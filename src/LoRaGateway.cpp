// ================= LoRaGateway.cpp =================
#include "LoRaGateway.h"
#include <ArduinoJson.h>

// Forward declaration — defined before sendGatewayBoot()
static void formatDuration(uint32_t ms, char* buf, size_t bufSize);

// ─────────────────────────────────────────────────────────────────────────────
// begin
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::begin(const char* gatewayName,
                        const char* key,
                        const char* mqttServer,
                        const char* mqttUser,
                        const char* mqttPass,
                        const char* ssid,
                        const char* wifiPassword,
                        bool        debug) {
    _debug = debug;

    _enableHeartbeat = false;

    LoRaNode::begin(key, gatewayName);

    _gatewayName  = gatewayName;
    _mqttUser     = mqttUser;
    _mqttPass     = mqttPass;
    _ssid         = ssid;
    _wifiPassword = wifiPassword;

    _mqtt.setServer(mqttServer, 1883);

    // Static trampoline : PubSubClient needs a plain function pointer;
    // we store 'this' in the callback via setCallback's user data trick.
    // PubSubClient doesn't support user data, so we use a lambda capturing this
    // stored in a static — only one gateway instance is expected on an ESP32.
    static LoRaGateway* _instance = nullptr;
    _instance = this;
    _mqtt.setCallback([](char* topic, uint8_t* payload, unsigned int length) {
        if (_instance) _instance->handleMqttIn(topic, payload, length);
    });

    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    if (_debug) Serial.printf("[GW] begin() - name='%s' mqtt=%s ssid=%s\n",
                  gatewayName, mqttServer, ssid);

    loadFromNVS();
}

// ─────────────────────────────────────────────────────────────────────────────
// loop
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::loop() {
    // Send GATEWAY_BOOT broadcast once from loop() - safe FreeRTOS context
    if (!_bootBroadcastDone) {
        _bootBroadcastDone = true;
        sendGatewayBoot();
    }

    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
        if (!_webStarted) {
            setupWebServer();
            _webStarted = true;
            if (_debug) Serial.printf("[GW] loop() - WiFi connected, IP=%s\n",
                          WiFi.localIP().toString().c_str());
        }
        connectMQTT();
        _mqtt.loop();
        _http.handleClient();
        _ws.loop();
        sendWsPing();
    }

    handleLoRa();
    checkPendingActuators();
    checkTimeouts();
    updateOfflineDurations();

    // Flush NVS from main loop context (safe for FreeRTOS semaphores)
    if (_nvsDirty) {
        _nvsDirty = false;
        saveToNVS();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WiFi / MQTT
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (millis() - _lastWifiAttempt < _wifiRetryInterval) return;
    _lastWifiAttempt = millis();
    if (_debug) Serial.println("[WiFi] Connecting...");
    WiFi.begin(_ssid, _wifiPassword);
}

void LoRaGateway::connectMQTT() {
    if (_mqtt.connected()) return;
    if (millis() - _lastMqttAttempt < _mqttRetryInterval) return;
    _lastMqttAttempt = millis();

    if (_debug) Serial.print("[MQTT] Connecting...");

    // LWT : <gateway>/OUT/<gateway>/status -> payload "0"
    char lwtTopic[96];
    snprintf(lwtTopic, sizeof(lwtTopic), "%s/OUT/%s/status", _gatewayName, _gatewayName);

    bool ok = _mqtt.connect(_gatewayName, _mqttUser, _mqttPass,
                            lwtTopic, 0, true, "0");
    if (ok) {
        if (_debug) Serial.println(" OK");
        _mqtt.publish(lwtTopic, "1", true);

        // Subscribe to all IN messages from the domotics controller
        char inTopic[64];
        snprintf(inTopic, sizeof(inTopic), "%s/IN/#", _gatewayName);
        _mqtt.subscribe(inTopic);
        if (_debug) Serial.printf("[MQTT] Subscribed to '%s'\n", inTopic);
    } else {
        if (_debug) Serial.printf(" Error rc=%d\n", _mqtt.state());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT IN handler
// Topic format : <gateway>/IN/<nodeName>/<sensorName>
// Payload      : numeric string (int or float)
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::handleMqttIn(const char* topic,
                                const uint8_t* payload, unsigned int len) {
    if (_debug) Serial.printf("[GW] handleMqttIn() - topic='%s'\n", topic);

    // Parse topic : skip "<gateway>/IN/"
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%s/IN/", _gatewayName);
    size_t prefixLen = strlen(prefix);

    if (strncmp(topic, prefix, prefixLen) != 0) return;

    // Remaining : "<nodeName>/<sensorName>"
    char rest[64];
    strncpy(rest, topic + prefixLen, sizeof(rest) - 1);
    rest[sizeof(rest) - 1] = '\0';

    char* slash = strchr(rest, '/');
    if (!slash) {
        if (_debug) Serial.println("[GW] handleMqttIn() - invalid topic format, ignored");
        return;
    }
    *slash = '\0';
    const char* nodeName   = rest;
    const char* sensorName = slash + 1;

    // Find node by name
    NodeInfo* n = nullptr;
    for (int i = 0; i < MAX_NODES; i++) {
        if (_nodes[i].id != 0 &&
            strncmp(_nodes[i].name, nodeName, NAME_LEN) == 0) {
            n = &_nodes[i];
            break;
        }
    }
    if (!n) {
        if (_debug) Serial.printf("[GW] handleMqttIn() - unknown node '%s'\n", nodeName);
        return;
    }

    // Find sensor by name
    SensorInfo* s = nullptr;
    for (int i = 0; i < n->sensorCount; i++) {
        if (strncmp(n->sensors[i].name, sensorName, NAME_LEN) == 0) {
            s = &n->sensors[i];
            break;
        }
    }
    if (!s) {
        if (_debug) Serial.printf("[GW] handleMqttIn() - unknown sensor '%s' on node '%s'\n",
                      sensorName, nodeName);
        return;
    }

    // Parse value from payload (null-terminate first)
    char valStr[16] = {};
    size_t copyLen = (len < sizeof(valStr) - 1) ? len : sizeof(valStr) - 1;
    memcpy(valStr, payload, copyLen);
    valStr[copyLen] = '\0';

    SensorValue value = {};
    switch ((DataType)s->dataType) {
        case TYPE_INT8:  value.asInt8  = (int8_t)atoi(valStr);  break;
        case TYPE_INT32: value.asInt32 = (int32_t)atol(valStr); break;
        case TYPE_FLOAT: value.asFloat = atof(valStr);          break;
        default:
            if (_debug) Serial.printf("[GW] handleMqttIn() - unknown dataType %d\n", s->dataType);
            return;
    }

    if (_debug) Serial.printf("[GW] handleMqttIn() - node='%s' sensor='%s' value='%s'\n",
                  nodeName, sensorName, valStr);

    queueActuator(n->id, s->id, s->dataType, value);
}

// ─────────────────────────────────────────────────────────────────────────────
// Actuator queue
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::queueActuator(uint32_t nodeID, uint8_t sensorID,
                                 uint8_t dataType, SensorValue value) {
    // Find a free slot
    for (int i = 0; i < MAX_PENDING_ACT; i++) {
        if (!_pendingAct[i].active) {
            _pendingAct[i].active   = true;
            _pendingAct[i].nodeID   = nodeID;
            _pendingAct[i].sensorID = sensorID;
            _pendingAct[i].dataType = dataType;
            _pendingAct[i].value    = value;
            _pendingAct[i].retries  = 0;
            _pendingAct[i].lastSent = 0;   // force immediate send
            if (_debug) Serial.printf("[GW] queueActuator() - nodeID=0x%08X sensorID=%d slot=%d\n",
                          nodeID, sensorID, i);
            return;
        }
    }
    if (_debug) Serial.println("[GW] queueActuator() - ERROR queue full");
}

void LoRaGateway::checkPendingActuators() {
    for (int i = 0; i < MAX_PENDING_ACT; i++) {
        PendingActuator& a = _pendingAct[i];
        if (!a.active) continue;

        if (millis() - a.lastSent < ACTUATOR_RETRY_INTERVAL) continue;

        if (a.retries >= RETRY_MAX) {
            if (_debug) Serial.printf("[GW] checkPendingActuators() - failed after %d attempts "
                          "nodeID=0x%08X sensorID=%d → NACK\n",
                          RETRY_MAX, a.nodeID, a.sensorID);
            publishNack(a.nodeID, a.sensorID);
            a.active = false;
            continue;
        }

        if (_debug) Serial.printf("[GW] checkPendingActuators() - sending attempt %d/%d "
                      "nodeID=0x%08X sensorID=%d\n",
                      a.retries + 1, RETRY_MAX, a.nodeID, a.sensorID);
        sendActuator(a);
        a.retries++;
        a.lastSent = millis();
    }
}

void LoRaGateway::sendActuator(const PendingActuator& a) {
    PayloadActuator p;
    p.nodeID   = a.nodeID;
    p.sensorID = a.sensorID;
    p.dataType = a.dataType;
    p.value    = a.value;
    sendFrame(MSG_ACTUATOR, a.nodeID, &p, sizeof(p), false);
}

void LoRaGateway::handleAckActuator(const uint8_t* payload, uint8_t len) {
    if (_debug) Serial.println("[GW] handleAckActuator() - ACK_ACTUATOR received");
    if (len < sizeof(PayloadAckActuator)) {
        if (_debug) Serial.println("[GW] handleAckActuator() - payload too short, ignored");
        return;
    }
    PayloadAckActuator p;
    memcpy(&p, payload, sizeof(p));

    for (int i = 0; i < MAX_PENDING_ACT; i++) {
        if (_pendingAct[i].active &&
            _pendingAct[i].nodeID   == p.nodeID &&
            _pendingAct[i].sensorID == p.sensorID) {
            if (_debug) Serial.printf("[GW] handleAckActuator() - ACK received slot=%d "
                          "nodeID=0x%08X sensorID=%d\n", i, p.nodeID, p.sensorID);
            _pendingAct[i].active = false;
            return;
        }
    }
    if (_debug) Serial.println("[GW] handleAckActuator() - ACK with no matching entry in queue");
}

void LoRaGateway::publishNack(uint32_t nodeID, uint8_t sensorID) {
    // No NACK published to MQTT — just log and discard
    if (_debug) Serial.printf("[GW] publishNack() - nodeID=0x%08X sensorID=%d: no ACK after %d attempts, dropped\n",
                              nodeID, sensorID, RETRY_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// LoRa frame reception
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::handleLoRa() {
    if (!radio.available()) return;

    uint8_t buf[64];
    uint8_t len = sizeof(buf);
    if (!radio.recv(buf, &len)) return;
    if (len < sizeof(FrameHeader)) return;

    FrameHeader h;
    memcpy(&h, buf, sizeof(h));

    if (h.key    != _key)    return;
    if (h.nodeID == _nodeID) return;

    if (_debug) Serial.printf("[GW] handleLoRa() - type=0x%02X nodeID=0x%08X rssi=%d snr=%.1f\n",
                  h.type, h.nodeID, radio.lastRssi(), radio.lastSNR());

    const uint8_t* payload    = buf + sizeof(FrameHeader);
    const uint8_t  payloadLen = len - (uint8_t)sizeof(FrameHeader);

    int   rssi = radio.lastRssi();
    float snr  = radio.lastSNR();

    switch (h.type) {
        case MSG_NODE_PRESENT:
            handleNodePresent(payload, payloadLen, rssi, snr);
            break;
        case MSG_SENSOR_PRESENT:
            handleSensorPresent(payload, payloadLen);
            break;
        case MSG_SENSOR:
            handleSensor(payload, payloadLen, rssi, snr);
            break;
        case MSG_HEARTBEAT:
            handleHeartbeat(payload, payloadLen);
            break;
        case MSG_ACK_ACTUATOR:
            handleAckActuator(payload, payloadLen);
            break;
        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame handlers
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::handleNodePresent(const uint8_t* payload, uint8_t len,
                                     int rssi, float snr) {
    if (_debug) Serial.println("[GW] handleNodePresent() - node presentation received");
    if (len < sizeof(PayloadNodePresent)) {
        if (_debug) Serial.println("[GW] handleNodePresent() - payload too short, ignored");
        return;
    }

    PayloadNodePresent p;
    memcpy(&p, payload, sizeof(p));
    p.name[NAME_LEN] = '\0';

    NodeInfo* n = getOrAddNode(p.nodeID);
    if (!n) {
        if (_debug) Serial.println("[GW] Node table full");
        return;
    }

    strncpy(n->name, p.name, NAME_LEN);
    n->name[NAME_LEN] = '\0';
    n->rssi     = rssi;
    n->snr      = snr;
    n->lastSeen = millis();

    if (_debug) Serial.printf("[GW] handleNodePresent() - node accepted: id=0x%08X name='%s' rssi=%d\n",
                  p.nodeID, n->name, rssi);

    if (!n->online) {
        n->online = true;
        snprintf(n->onlineDuration,  sizeof(n->onlineDuration),  "just now");
        snprintf(n->offlineDuration, sizeof(n->offlineDuration), "");
        publishNodeStatus(*n, "online");
    }

    _nvsDirty = true;
    sendAckNode(p.nodeID);
    broadcastState();
}

void LoRaGateway::handleSensorPresent(const uint8_t* payload, uint8_t len) {
    if (_debug) Serial.println("[GW] handleSensorPresent() - sensor presentation received");
    if (len < sizeof(PayloadSensorPresent)) {
        if (_debug) Serial.println("[GW] handleSensorPresent() - payload too short, ignored");
        return;
    }

    PayloadSensorPresent p;
    memcpy(&p, payload, sizeof(p));
    p.name[NAME_LEN] = '\0';

    NodeInfo* n = getNode(p.nodeID);
    if (!n) {
        if (_debug) Serial.printf("[GW] Sensor rejected: unknown node 0x%08X\n", p.nodeID);
        return;
    }

    SensorInfo* s = getOrAddSensor(n, p.sensorID);
    if (!s) {
        if (_debug) Serial.println("[GW] Sensor table full");
        return;
    }

    s->id       = p.sensorID;
    s->dataType = p.dataType;
    strncpy(s->name, p.name, NAME_LEN);
    s->name[NAME_LEN] = '\0';

    if (_debug) Serial.printf("[GW] handleSensorPresent() - sensor accepted: "
                  "nodeID=0x%08X sensorID=%d nom='%s' type=%d\n",
                  p.nodeID, p.sensorID, s->name, p.dataType);

    _nvsDirty = true;
    sendAckSensor(p.nodeID, p.sensorID);
    broadcastState();
}

void LoRaGateway::handleSensor(const uint8_t* payload, uint8_t len,
                                int rssi, float snr) {
    if (_debug) Serial.println("[GW] handleSensor() - sensor value received");
    if (len < sizeof(PayloadSensor)) {
        if (_debug) Serial.println("[GW] handleSensor() - payload too short, ignored");
        return;
    }

    PayloadSensor p;
    memcpy(&p, payload, sizeof(p));

    NodeInfo* n = getNode(p.nodeID);
    if (!n) {
        if (_debug) Serial.printf("[GW] handleSensor() - unknown node 0x%08X, ignored\n", p.nodeID);
        return;
    }

    SensorInfo* s = getSensor(n, p.sensorID);
    if (!s) {
        if (_debug) Serial.printf("[GW] handleSensor() - unknown sensor %d on node '%s', ignored\n",
                      p.sensorID, n->name);
        return;
    }

    memcpy(&s->lastValue, &p.value, sizeof(s->lastValue));
    s->hasValue = true;

    if (_debug) Serial.printf("[GW] handleSensor() - node='%s' sensor='%s' type=%d\n",
                  n->name, s->name, s->dataType);

    updateNodeStats(p.nodeID, rssi, snr);

    if (!n->online) {
        if (_debug) Serial.printf("[GW] handleSensor() - node '%s' back online\n", n->name);
        n->online = true;
        snprintf(n->onlineDuration,  sizeof(n->onlineDuration),  "just now");
        snprintf(n->offlineDuration, sizeof(n->offlineDuration), "");
        publishNodeStatus(*n, "online");
    }

    _nvsDirty = true;
    publishSensorValue(*n, *s);
    broadcastState();
}

void LoRaGateway::handleHeartbeat(const uint8_t* payload, uint8_t len) {
    if (_debug) Serial.println("[GW] handleHeartbeat() - heartbeat received");
    if (len < sizeof(PayloadHeartbeat)) {
        if (_debug) Serial.println("[GW] handleHeartbeat() - payload too short, ignored");
        return;
    }

    PayloadHeartbeat p;
    memcpy(&p, payload, sizeof(p));

    NodeInfo* n = getNode(p.nodeID);
    if (!n) {
        if (_debug) Serial.printf("[GW] handleHeartbeat() - unknown node 0x%08X, ignored\n", p.nodeID);
        return;
    }

    n->battery  = p.battery;
    n->uptime   = p.uptime;
    n->lastSeen = millis();

    if (_debug) Serial.printf("[GW] handleHeartbeat() - node='%s' battery=%d%% uptime=%lums\n",
                  n->name, p.battery, p.uptime);

    // Update online duration using node uptime from heartbeat
    if (n->online) {
        formatDuration(p.uptime, n->onlineDuration, sizeof(n->onlineDuration));
    }

    // Publish battery level to MQTT
    if (_mqtt.connected()) {
        char batTopic[96];
        char batVal[8];
        snprintf(batTopic, sizeof(batTopic), "%s/OUT/%s/battery", _gatewayName, n->name);
        snprintf(batVal,   sizeof(batVal),   "%d", n->battery);
        _mqtt.publish(batTopic, batVal, false);
        if (_debug) Serial.printf("[GW] handleHeartbeat() - battery topic='%s' val='%s'\n", batTopic, batVal);
    }

    _nvsDirty = true;

    if (!n->online) {
        if (_debug) Serial.printf("[GW] handleHeartbeat() - node '%s' back online\n", n->name);
        n->online = true;
        snprintf(n->onlineDuration,  sizeof(n->onlineDuration),  "just now");
        snprintf(n->offlineDuration, sizeof(n->offlineDuration), "");
        publishNodeStatus(*n, "online");
    }

    broadcastState();
}

// ─────────────────────────────────────────────────────────────────────────────
// ACK senders
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::sendAckNode(uint32_t nodeID) {
    if (_debug) Serial.printf("[GW] sendAckNode() - ACK_NODE -> 0x%08X\n", nodeID);
    PayloadAckNode p;
    p.nodeID = nodeID;
    sendFrame(MSG_ACK_NODE, nodeID, &p, sizeof(p), false);
}

void LoRaGateway::sendAckSensor(uint32_t nodeID, uint8_t sensorID) {
    if (_debug) Serial.printf("[GW] sendAckSensor() - ACK_SENSOR -> nodeID=0x%08X sensorID=%d\n",
                  nodeID, sensorID);
    PayloadAckSensor p;
    p.nodeID   = nodeID;
    p.sensorID = sensorID;
    sendFrame(MSG_ACK_SENSOR, nodeID, &p, sizeof(p), false);
}

void LoRaGateway::sendReboot(uint32_t nodeID) {
    if (_debug) Serial.printf("[GW] sendReboot() - REBOOT -> nodeID=0x%08X\n", nodeID);
    PayloadReboot p;
    p.nodeID = nodeID;
    sendFrame(MSG_REBOOT, nodeID, &p, sizeof(p), false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Node/Sensor registry helpers
// ─────────────────────────────────────────────────────────────────────────────
LoRaGateway::NodeInfo* LoRaGateway::getNode(uint32_t id) {
    for (int i = 0; i < MAX_NODES; i++)
        if (_nodes[i].id == id) return &_nodes[i];
    return nullptr;
}

LoRaGateway::NodeInfo* LoRaGateway::getOrAddNode(uint32_t id) {
    NodeInfo* n = getNode(id);
    if (n) return n;
    for (int i = 0; i < MAX_NODES; i++) {
        if (_nodes[i].id == 0) {
            _nodes[i] = NodeInfo{};
            _nodes[i].id = id;
            return &_nodes[i];
        }
    }
    return nullptr;
}

LoRaGateway::SensorInfo* LoRaGateway::getSensor(NodeInfo* n, uint8_t sensorID) {
    for (int i = 0; i < n->sensorCount; i++)
        if (n->sensors[i].id == sensorID) return &n->sensors[i];
    return nullptr;
}

LoRaGateway::SensorInfo* LoRaGateway::getOrAddSensor(NodeInfo* n, uint8_t sensorID) {
    SensorInfo* s = getSensor(n, sensorID);
    if (s) return s;
    if (n->sensorCount >= MAX_SENSORS) return nullptr;
    SensorInfo& ns = n->sensors[n->sensorCount++];
    ns = SensorInfo{};
    ns.id = sensorID;
    return &ns;
}

void LoRaGateway::removeNode(uint32_t id) {
    if (_debug) Serial.printf("[GW] removeNode() - removing node 0x%08X\n", id);
    for (int i = 0; i < MAX_NODES; i++) {
        if (_nodes[i].id == id) {
            _nodes[i] = NodeInfo{};
            _nvsDirty = true;
            return;
        }
    }
}

void LoRaGateway::updateNodeStats(uint32_t id, int rssi, float snr) {
    NodeInfo* n = getNode(id);
    if (!n) return;
    n->rssi     = (n->rssi + rssi) / 2;
    n->snr      = (n->snr  + snr)  / 2.0f;
    n->lastSeen = millis();
}

void LoRaGateway::checkTimeouts() {
    for (int i = 0; i < MAX_NODES; i++) {
        if (_nodes[i].id == 0) continue;
        if (_nodes[i].online && millis() - _nodes[i].lastSeen > 300000UL) {
            if (_debug) Serial.printf("[GW] checkTimeouts() - node '%s' timed out -> offline\n",
                          _nodes[i].name);
            _nodes[i].online    = false;
            _nodes[i].offlineAt = millis();
            snprintf(_nodes[i].offlineDuration, sizeof(_nodes[i].offlineDuration), "just now");
            snprintf(_nodes[i].onlineDuration,  sizeof(_nodes[i].onlineDuration),  "");
            publishNodeStatus(_nodes[i], "offline");
            broadcastState();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MQTT publishing
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::publishNodeStatus(const NodeInfo& n, const char* status) {
    if (!_mqtt.connected()) return;
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/OUT/%s/status", _gatewayName, n.name);
    // Publish 1 for online, 0 for offline
    const char* val = (strcmp(status, "online") == 0) ? "1" : "0";
    if (_debug) Serial.printf("[GW] publishNodeStatus() - topic='%s' val='%s'\n", topic, val);
    _mqtt.publish(topic, val, true);
}

void LoRaGateway::publishSensorValue(const NodeInfo& n, const SensorInfo& s) {
    if (!_mqtt.connected()) return;

    char topic[96];
    snprintf(topic, sizeof(topic), "%s/OUT/%s/%s",
             _gatewayName, n.name, s.name);

    char valStr[16];
    switch ((DataType)s.dataType) {
        case TYPE_INT8:  snprintf(valStr, sizeof(valStr), "%d",   s.lastValue.asInt8);          break;
        case TYPE_INT32: snprintf(valStr, sizeof(valStr), "%ld",  (long)s.lastValue.asInt32);   break;
        case TYPE_FLOAT: snprintf(valStr, sizeof(valStr), "%.2f", s.lastValue.asFloat);         break;
        default: return;
    }

    if (_debug) Serial.printf("[GW] publishSensorValue() - topic='%s' val='%s'\n", topic, valStr);
    _mqtt.publish(topic, valStr, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Web server setup
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::setupWebServer() {
    _http.on("/", [this]() { handleHttpRoot(); });
    _http.on("/favicon.ico", [this]() { _http.send(204); });
    _http.onNotFound([this]() { _http.send(404, "text/plain", "Not found"); });
    _http.begin();

    _ws.begin();
    _ws.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        handleWsEvent(num, type, payload, length);
    });

    if (_debug) Serial.println("[HTTP] Server started on port 80");
    if (_debug) Serial.println("[WS]   WebSocket started on port 81");
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP : single-page app
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::handleHttpRoot() {
    _http.send(200, "text/html", R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LoRa Gateway</title>
<link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Ccircle cx='16' cy='10' r='3.5' fill='%2338bdf8'/%3E%3Ccircle cx='8' cy='22' r='2.8' fill='%2338bdf8' opacity='0.7'/%3E%3Ccircle cx='24' cy='22' r='2.8' fill='%2338bdf8' opacity='0.7'/%3E%3Ccircle cx='16' cy='25' r='2' fill='%2338bdf8' opacity='0.45'/%3E%3Cline x1='16' y1='13.5' x2='9.5' y2='19.5' stroke='%2338bdf8' stroke-width='1.2' opacity='0.6'/%3E%3Cline x1='16' y1='13.5' x2='22.5' y2='19.5' stroke='%2338bdf8' stroke-width='1.2' opacity='0.6'/%3E%3Cline x1='10.5' y1='24' x2='14' y2='24' stroke='%2338bdf8' stroke-width='1.2' opacity='0.4'/%3E%3Cline x1='18' y1='24' x2='21.5' y2='24' stroke='%2338bdf8' stroke-width='1.2' opacity='0.4'/%3E%3C/svg%3E">
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh}
  header{background:#1e293b;padding:1rem 1.5rem;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #334155}
  h1{font-size:1.2rem;font-weight:600;color:#38bdf8}
  h1 span{color:#38bdf8;margin-left:.4rem}
  .badge{font-size:.7rem;padding:.2rem .5rem;border-radius:999px;font-weight:600}
  .online{background:#052e16;color:#4ade80}.offline{background:#450a0a;color:#f87171}
  main{padding:1.5rem;display:grid;gap:1rem;grid-template-columns:repeat(auto-fill,minmax(340px,1fr))}
  .card{background:#1e293b;border-radius:.75rem;border:1px solid #334155;overflow:hidden}
  .card-header{padding:.75rem 1rem;background:#0f172a}
  .node-name{font-weight:600;font-size:1rem}
  .node-meta{font-size:.72rem;color:#94a3b8;display:flex;gap:.75rem;flex-wrap:wrap;margin-top:.2rem}
  .actions{display:flex;gap:.5rem;margin-top:.5rem}
  button{border:none;border-radius:.4rem;padding:.35rem .75rem;font-size:.75rem;cursor:pointer;font-weight:600;transition:opacity .15s}
  button:hover{opacity:.8}
  .btn-reboot{background:#1d4ed8;color:#fff}
  .btn-delete{background:#991b1b;color:#fff}
  .btn-gw{background:#7c3aed;color:#fff;padding:.4rem .9rem;font-size:.8rem}
  table{width:100%;border-collapse:collapse;font-size:.8rem}
  th{text-align:left;padding:.5rem 1rem;color:#94a3b8;font-weight:500;border-bottom:1px solid #334155}
  td{padding:.5rem 1rem;border-bottom:1px solid #1e293b}
  .val-float{color:#34d399}.val-int32{color:#60a5fa}.val-int8{color:#f472b6}
  .empty{padding:1rem;color:#64748b;font-size:.85rem;text-align:center}
  #ws-status{font-size:.72rem;padding:.15rem .5rem;border-radius:999px}
  .ws-ok{background:#052e16;color:#4ade80}.ws-err{background:#450a0a;color:#f87171}
</style>
</head>
<body>
<header>
  <div><h1><svg width="22" height="22" viewBox="0 0 32 32" style="vertical-align:middle;margin-right:6px"><circle cx="16" cy="10" r="3.5" fill="#38bdf8"/><circle cx="8" cy="22" r="2.8" fill="#38bdf8" opacity="0.7"/><circle cx="24" cy="22" r="2.8" fill="#38bdf8" opacity="0.7"/><circle cx="16" cy="25" r="2" fill="#38bdf8" opacity="0.45"/><line x1="16" y1="13.5" x2="9.5" y2="19.5" stroke="#38bdf8" stroke-width="1.2" opacity="0.6"/><line x1="16" y1="13.5" x2="22.5" y2="19.5" stroke="#38bdf8" stroke-width="1.2" opacity="0.6"/><line x1="10.5" y1="24" x2="14" y2="24" stroke="#38bdf8" stroke-width="1.2" opacity="0.4"/><line x1="18" y1="24" x2="21.5" y2="24" stroke="#38bdf8" stroke-width="1.2" opacity="0.4"/></svg>LoRa Gateway<span id="gw-name"></span></h1></div>
  <div style="display:flex;align-items:center;gap:.75rem">
    <span id="ws-status" class="ws-err">Gateway offline</span>
    <button class="btn-gw" onclick="sendCmd({cmd:'reboot_gw'})">&#x21BA; Reboot gateway</button>
  </div>
</header>
<main id="nodes-grid"><p class="empty" style="grid-column:1/-1">Waiting for data...</p></main>

<script>
let ws;
let state = {};
let lastPing = 0;

function setStatus(text, ok) {
  const el = document.getElementById('ws-status');
  if (!el) return;
  el.textContent = text;
  el.className = ok ? 'ws-ok' : 'ws-err';
}

setInterval(() => {
  if (lastPing === 0) return;
  if (Date.now() - lastPing > 7000) setStatus('Gateway offline', false);
}, 2000);

function connect() {
  ws = new WebSocket('ws://' + location.hostname + ':81');
  ws.onopen = () => { setStatus('Connecting...', false); };
  ws.onclose = () => {
    lastPing = 0;
    setStatus('Gateway offline', false);
    setTimeout(connect, 1000);
  };
  ws.onerror = () => {
    lastPing = 0;
    setStatus('Gateway offline', false);
  };
  ws.onmessage = e => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === 'ping') {
        lastPing = Date.now();
        setStatus('Gateway online', true);
        return;
      }
      state = msg;
      render(state);
    } catch(_) {}
  };
}

function sendCmd(obj) { if(ws && ws.readyState===1) ws.send(JSON.stringify(obj)); }

function typeLabel(t) { return ['int8','int32','float'][t] || '?'; }
function typeClass(t) { return ['val-int8','val-int32','val-float'][t] || ''; }

function formatVal(sensor) {
  if (!sensor.hasValue) return '<span style="color:#475569">--</span>';
  const cls = typeClass(sensor.dataType);
  const str = sensor.dataType === 2 ? Number(sensor.value).toFixed(2) : String(sensor.value);
  return `<span class="${cls}">${str}</span>`;
}

function render(s) {
  if (s.gateway) document.getElementById('gw-name').textContent = ' ' + s.gateway;
  const grid = document.getElementById('nodes-grid');
  if (!s.nodes || s.nodes.length === 0) {
    grid.innerHTML = '<p class="empty" style="grid-column:1/-1">No registered nodes</p>';
    return;
  }
  grid.innerHTML = s.nodes.map(n => {
    const dur = n.online
      ? (n.onlineDuration  ? 'online since '  + n.onlineDuration  : 'online')
      : (n.offlineDuration ? 'offline since ' + n.offlineDuration : 'offline');
    return `
    <div class="card">
      <div class="card-header">
        <div style="display:flex;align-items:center;gap:.5rem">
          <span class="node-name">${n.name}</span>
          <span class="badge ${n.online?'online':'offline'}">${dur}</span>
        </div>
        <div class="node-meta">
          <span>ID&nbsp;${'0x'+n.id.toString(16).padStart(8,'0').toUpperCase()}</span>
          <span>RSSI&nbsp;${n.rssi}&nbsp;dBm</span>
          <span>SNR&nbsp;${Number(n.snr).toFixed(1)}&nbsp;dB</span>
          <span>&#x1F50B;&nbsp;${n.battery}%</span>
        </div>
        <div class="actions">
          <button class="btn-reboot" onclick="sendCmd({cmd:'reboot_node',id:${n.id}})">&#x21BA; Reboot</button>
          <button class="btn-delete" onclick="sendCmd({cmd:'delete_node',id:${n.id}})">&#x1F5D1; Delete</button>
        </div>
      </div>
      ${n.sensors && n.sensors.length > 0 ? `
      <table>
        <thead><tr><th>Name</th><th>ID</th><th>Type</th><th>Value</th></tr></thead>
        <tbody>
          ${n.sensors.map(s => `
          <tr>
            <td>${s.name}</td>
            <td>${s.id}</td>
            <td>${typeLabel(s.dataType)}</td>
            <td>${formatVal(s)}</td>
          </tr>`).join('')}
        </tbody>
      </table>` : '<p class="empty">No sensors</p>'}
    </div>`;
  }).join('');
}

connect();
</script>
</body>
</html>
)rawhtml");
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket event handler
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::handleWsEvent(uint8_t num, WStype_t type,
                                 uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            if (_debug) Serial.printf("[WS] Client #%u connected\n", num);
            { String s = buildStateJson(); _ws.sendTXT(num, s); }
            break;

        case WStype_TEXT: {
            if (_debug) Serial.printf("[GW] handleWsEvent() - WS command from client #%u\n", num);
            JsonDocument doc;
            if (deserializeJson(doc, payload, length)) return;

            const char* cmd = doc["cmd"];
            if (!cmd) return;

            if (strcmp(cmd, "reboot_gw") == 0) {
                if (_debug) Serial.println("[WS] Rebooting gateway");
                delay(200);
                ESP.restart();

            } else if (strcmp(cmd, "reboot_node") == 0) {
                uint32_t id = doc["id"] | 0;
                if (id) sendReboot(id);

            } else if (strcmp(cmd, "delete_node") == 0) {
                uint32_t id = doc["id"] | 0;
                if (id) {
                    NodeInfo* n = getNode(id);
                    if (n) {
                        publishNodeStatus(*n, "offline");
                        removeNode(id);
                        broadcastState();
                    }
                }
            }
            break;
        }

        case WStype_DISCONNECTED:
            if (_debug) Serial.printf("[WS] Client #%u disconnected\n", num);
            break;

        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Gateway boot broadcast
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Format a millisecond duration into "Xd Xh Xmin" or "Xs"
// ─────────────────────────────────────────────────────────────────────────────
static void formatDuration(uint32_t ms, char* buf, size_t bufSize) {
    uint32_t s = ms / 1000;
    uint32_t m = s  / 60;
    uint32_t h = m  / 60;
    uint32_t d = h  / 24;
    if (d > 0)      snprintf(buf, bufSize, "%lud %luh", (unsigned long)d, (unsigned long)(h % 24));
    else if (h > 0) snprintf(buf, bufSize, "%luh %lumin", (unsigned long)h, (unsigned long)(m % 60));
    else if (m > 0) snprintf(buf, bufSize, "%lumin", (unsigned long)m);
    else            snprintf(buf, bufSize, "%lus", (unsigned long)s);
}

void LoRaGateway::sendGatewayBoot() {
    if (_debug) Serial.println("[GW] sendGatewayBoot() - broadcasting MSG_GATEWAY_BOOT");
    // No payload needed — just header broadcast
    sendFrame(MSG_GATEWAY_BOOT, 0, nullptr, 0, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update offline duration strings for all offline nodes (called from loop)
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::updateOfflineDurations() {
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate < 30000UL) return;
    lastUpdate = millis();

    bool changed = false;
    for (int i = 0; i < MAX_NODES; i++) {
        if (_nodes[i].id == 0 || _nodes[i].online) continue;
        if (_nodes[i].offlineAt == 0) continue;
        char buf[24];
        formatDuration(millis() - _nodes[i].offlineAt, buf, sizeof(buf));
        if (strcmp(buf, _nodes[i].offlineDuration) != 0) {
            strncpy(_nodes[i].offlineDuration, buf, sizeof(_nodes[i].offlineDuration));
            changed = true;
        }
    }
    if (changed) broadcastState();
}

// ─────────────────────────────────────────────────────────────────────────────
// NVS Persistence
// Layout :
//   "n_count"         → uint8  : number of nodes
//   "n<i>_id"         → uint32 : node ID
//   "n<i>_name"       → string : node name
//   "n<i>_sc"         → uint8  : sensor count
//   "n<i>s<j>_id"     → uint8  : sensor ID
//   "n<i>s<j>_type"   → uint8  : sensor dataType
//   "n<i>s<j>_name"   → string : sensor name
// ─────────────────────────────────────────────────────────────────────────────
void LoRaGateway::saveToNVS() {
    _prefs.begin("loradomo", false);
    _prefs.clear();

    uint8_t nodeCount = 0;
    for (int i = 0; i < MAX_NODES; i++)
        if (_nodes[i].id != 0) nodeCount++;

    _prefs.putUChar("n_count", nodeCount);

    uint8_t ni = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        const NodeInfo& n = _nodes[i];
        if (n.id == 0) continue;

        char key[16];
        snprintf(key, sizeof(key), "n%d_id", ni);
        _prefs.putULong(key, n.id);

        snprintf(key, sizeof(key), "n%d_name", ni);
        _prefs.putString(key, n.name);

        snprintf(key, sizeof(key), "n%d_sc", ni);
        _prefs.putUChar(key, n.sensorCount);

        for (int j = 0; j < n.sensorCount; j++) {
            const SensorInfo& s = n.sensors[j];

            snprintf(key, sizeof(key), "n%ds%d_id", ni, j);
            _prefs.putUChar(key, s.id);

            snprintf(key, sizeof(key), "n%ds%d_type", ni, j);
            _prefs.putUChar(key, s.dataType);

            snprintf(key, sizeof(key), "n%ds%d_name", ni, j);
            _prefs.putString(key, s.name);
        }
        ni++;
    }

    _prefs.end();
    if (_debug) Serial.printf("[GW] saveToNVS() - %d node(s) saved\n", nodeCount);
}

void LoRaGateway::loadFromNVS() {
    _prefs.begin("loradomo", true);   // read-only

    uint8_t nodeCount = _prefs.getUChar("n_count", 0);
    if (_debug) Serial.printf("[GW] loadFromNVS() - %d node(s) found\n", nodeCount);

    for (uint8_t ni = 0; ni < nodeCount && ni < MAX_NODES; ni++) {
        char key[16];

        snprintf(key, sizeof(key), "n%d_id", ni);
        uint32_t id = _prefs.getULong(key, 0);
        if (id == 0) continue;

        NodeInfo* n = getOrAddNode(id);
        if (!n) continue;

        snprintf(key, sizeof(key), "n%d_name", ni);
        String name = _prefs.getString(key, "");
        strncpy(n->name, name.c_str(), NAME_LEN);
        n->name[NAME_LEN] = '\0';
        n->online    = false;
        n->offlineAt = millis();   // approximate — gateway just rebooted
        snprintf(n->offlineDuration, sizeof(n->offlineDuration), "just now");

        snprintf(key, sizeof(key), "n%d_sc", ni);
        uint8_t sc = _prefs.getUChar(key, 0);

        for (uint8_t j = 0; j < sc && j < MAX_SENSORS; j++) {
            snprintf(key, sizeof(key), "n%ds%d_id", ni, j);
            uint8_t sid = _prefs.getUChar(key, 0);

            snprintf(key, sizeof(key), "n%ds%d_type", ni, j);
            uint8_t stype = _prefs.getUChar(key, TYPE_FLOAT);

            snprintf(key, sizeof(key), "n%ds%d_name", ni, j);
            String sname = _prefs.getString(key, "");

            SensorInfo* s = getOrAddSensor(n, sid);
            if (!s) continue;
            s->id       = sid;
            s->dataType = stype;
            strncpy(s->name, sname.c_str(), NAME_LEN);
            s->name[NAME_LEN] = '\0';
        }

        if (_debug) Serial.printf("[GW] loadFromNVS() - node '%s' (0x%08X) %d sensor(s)\n",
                      n->name, n->id, n->sensorCount);
    }

    _prefs.end();
}

// ─────────────────────────────────────────────────────────────────────────────
// Build JSON state
// ─────────────────────────────────────────────────────────────────────────────
String LoRaGateway::buildStateJson() {
    JsonDocument doc;
    doc["gateway"] = _gatewayName;

    JsonArray nodesArr = doc["nodes"].to<JsonArray>();

    for (int i = 0; i < MAX_NODES; i++) {
        const NodeInfo& n = _nodes[i];
        if (n.id == 0) continue;

        JsonObject nObj = nodesArr.add<JsonObject>();
        nObj["id"]      = n.id;
        nObj["name"]    = n.name;
        nObj["online"]  = n.online;
        nObj["rssi"]    = n.rssi;
        nObj["snr"]     = serialized(String(n.snr, 1));
        nObj["battery"]      = n.battery;
        nObj["uptime"]       = n.uptime;
        nObj["onlineDuration"]  = n.onlineDuration;
        nObj["offlineDuration"] = n.offlineDuration;

        JsonArray sensorsArr = nObj["sensors"].to<JsonArray>();
        for (int j = 0; j < n.sensorCount; j++) {
            const SensorInfo& s = n.sensors[j];
            JsonObject sObj = sensorsArr.add<JsonObject>();
            sObj["id"]       = s.id;
            sObj["name"]     = s.name;
            sObj["dataType"] = s.dataType;
            sObj["hasValue"] = s.hasValue;

            if (s.hasValue) {
                switch ((DataType)s.dataType) {
                    case TYPE_INT8:  sObj["value"] = s.lastValue.asInt8;  break;
                    case TYPE_INT32: sObj["value"] = s.lastValue.asInt32; break;
                    case TYPE_FLOAT: sObj["value"] = serialized(String(s.lastValue.asFloat, 2)); break;
                    default: break;
                }
            }
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void LoRaGateway::sendWsPing() {
    if (_ws.connectedClients() == 0) return;
    if (millis() - _lastWsPing < _wsPingInterval) return;
    _lastWsPing = millis();
    _ws.broadcastTXT("{\"type\":\"ping\"}");
}

void LoRaGateway::broadcastState() {
    if (_ws.connectedClients() == 0) return;
    String s = buildStateJson();
    _ws.broadcastTXT(s);
}
