// ================= LoRaNode.cpp =================
#include "LoRaNode.h"

// ─────────────────────────────────────────────────────────────────────────────
// Radio configuration — public setters
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::setFrequency(float freq) {
    _frequency = freq;
    radio.setFrequency(freq);
    if (_debug) Serial.printf("[Node] setFrequency() - %.1f MHz\n", freq);
}

void LoRaNode::setTxPower(int dbm) {
    _txPower = dbm;
    radio.setTxPower(dbm);
    if (_debug) Serial.printf("[Node] setTxPower() - %d dBm\n", dbm);
}

void LoRaNode::setModemConfig(LoRaModemConfig config) {
    _modemConfig = config;
    applyModemConfig();
    if (_debug) Serial.printf("[Node] setModemConfig() - config=%d\n", config);
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal radio init — called from begin() and setters
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::applyRadioConfig() {
#if defined(LORADOMO_USE_SX1276)
    radio.setFrequency(_frequency);
    radio.setTxPower(_txPower);
    applyModemConfig();
#else
    radio.setTCXO(3.3f, 100);
    radio.setFrequency(_frequency);
    radio.setTxPower(_txPower);
    applyModemConfig();
#endif
}

void LoRaNode::applyModemConfig() {
#if defined(LORADOMO_USE_SX1276)
    // RH_RF95 modem configs
    switch (_modemConfig) {
        case MODEM_BW125_CR45_SF128:  radio.setModemConfig(RH_RF95::Bw125Cr45Sf128);   break;
        case MODEM_BW500_CR45_SF128:  radio.setModemConfig(RH_RF95::Bw500Cr45Sf128);   break;
        case MODEM_BW31_CR48_SF512:   radio.setModemConfig(RH_RF95::Bw31_25Cr48Sf512); break;
        case MODEM_BW125_CR48_SF4096: radio.setModemConfig(RH_RF95::Bw125Cr48Sf4096);  break;
    }
#else
    // RH_SX126x modem configs
    switch (_modemConfig) {
        case MODEM_BW125_CR45_SF128:  radio.setModemConfig(RH_SX126x::LoRa_Bw125Cr45Sf128);   break;
        case MODEM_BW500_CR45_SF128:  radio.setModemConfig(RH_SX126x::LoRa_Bw500Cr45Sf128);   break;
        case MODEM_BW31_CR48_SF512:   radio.setModemConfig(RH_SX126x::LoRa_Bw31_25Cr48Sf512); break;
        case MODEM_BW125_CR48_SF4096: radio.setModemConfig(RH_SX126x::LoRa_Bw125Cr48Sf4096);  break;
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Key hashing : FNV-1a XOR-folded to 16 bits
// ─────────────────────────────────────────────────────────────────────────────
uint16_t LoRaNode::hashKey(const char* key) {
    uint32_t hash = 2166136261UL;
    while (*key) {
        hash ^= (uint8_t)*key++;
        hash *= 16777619UL;
    }
    return (uint16_t)((hash >> 16) ^ (hash & 0xFFFF));
}

// ─────────────────────────────────────────────────────────────────────────────
// begin
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::begin(const char* key, const char* nodeName, bool debug) {
    _debug = debug;
    // Use full 64-bit EFuse MAC hashed to 32 bits via FNV-1a
    // — avoids duplicates when lower 32 bits are identical (common on ESP32 V1/V2)
    uint64_t mac = ESP.getEfuseMac();
    uint32_t lo  = (uint32_t)(mac & 0xFFFFFFFF);
    uint32_t hi  = (uint32_t)(mac >> 32);
    // XOR-fold and mix both halves
    _nodeID = lo ^ (hi * 2654435761UL);  // Knuth multiplicative hash
    if (_nodeID == 0) _nodeID = 0xDEADBEEF;  // safety: avoid zero ID
    _key    = hashKey(key);

    strncpy(_nodeName, nodeName, NAME_LEN);
    _nodeName[NAME_LEN] = '\0';

    if (!radio.init()) {
        if (_debug) Serial.println("[Node] begin() - ERROR radio init failed");
    }
    applyRadioConfig();

    if (_debug) Serial.printf("[Node] begin() - board=%s radio OK freq=%.1fMHz tx=%ddBm\n",
                  LORADOMO_BOARD_NAME, _frequency, _txPower);

    _state              = STATE_UNREGISTERED;
    _justRegistered     = false;
    _lastPresentAttempt = 0;
    _currentSensorIdx   = 0;

    if (_debug) Serial.printf("[Node] begin() - name='%s' id=0x%08X key=0x%04X\n",
                  _nodeName, _nodeID, _key);
}

// ─────────────────────────────────────────────────────────────────────────────
// addSensor
// ─────────────────────────────────────────────────────────────────────────────
bool LoRaNode::addSensor(uint8_t sensorID, DataType type, const char* name,
                          uint32_t sendInterval, void* actCallback, void* readCallback) {
    if (_sensorCount >= MAX_SENSORS) {
        if (_debug) Serial.printf("[Node] addSensor() - ERROR table full (id=%d)\n", sensorID);
        return false;
    }

    SensorEntry& s  = _sensors[_sensorCount++];
    s.id            = sensorID;
    s.type          = type;
    s.acked         = false;
    s.actCallback   = actCallback;
    s.readCallback  = readCallback;
    s.hasValue      = false;
    s.sendInterval  = sendInterval;
    s.lastSent      = 0;
    strncpy(s.name, name, NAME_LEN);
    s.name[NAME_LEN] = '\0';

    if (_debug) Serial.printf("[Node] addSensor() - id=%d type=%d name='%s' interval=%lus act=%s read=%s\n",
                  sensorID, type, name, sendInterval,
                  actCallback  ? "yes" : "no",
                  readCallback ? "yes" : "no");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// setSensorXxx — store value without sending (e.g. from EEPROM in setup())
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::setSensorInt8(uint8_t sensorID, int8_t value) {
    SensorEntry* s = findSensor(sensorID);
    if (!s) return;
    storeSensorValue(*s, &value, sizeof(value));
    if (_debug) Serial.printf("[Node] setSensorInt8() - sensorID=%d value=%d\n", sensorID, value);
}

void LoRaNode::setSensorInt32(uint8_t sensorID, int32_t value) {
    SensorEntry* s = findSensor(sensorID);
    if (!s) return;
    storeSensorValue(*s, &value, sizeof(value));
    if (_debug) Serial.printf("[Node] setSensorInt32() - sensorID=%d value=%ld\n", sensorID, (long)value);
}

void LoRaNode::setSensorFloat(uint8_t sensorID, float value) {
    SensorEntry* s = findSensor(sensorID);
    if (!s) return;
    storeSensorValue(*s, &value, sizeof(value));
    if (_debug) Serial.printf("[Node] setSensorFloat() - sensorID=%d value=%.2f\n", sensorID, value);
}

// ─────────────────────────────────────────────────────────────────────────────
// sendXxx — manual immediate send, resets interval timer
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::sendInt8(uint8_t sensorID, int8_t value) {
    if (_state != STATE_REGISTERED) return;
    SensorEntry* s = findSensor(sensorID);
    if (!s || !s->acked) return;
    if (_debug) Serial.printf("[Node] sendInt8() - sensorID=%d value=%d\n", sensorID, value);
    storeSensorValue(*s, &value, sizeof(value));
    transmitSensor(*s);
}

void LoRaNode::sendInt32(uint8_t sensorID, int32_t value) {
    if (_state != STATE_REGISTERED) return;
    SensorEntry* s = findSensor(sensorID);
    if (!s || !s->acked) return;
    if (_debug) Serial.printf("[Node] sendInt32() - sensorID=%d value=%ld\n", sensorID, (long)value);
    storeSensorValue(*s, &value, sizeof(value));
    transmitSensor(*s);
}

void LoRaNode::sendFloat(uint8_t sensorID, float value) {
    if (_state != STATE_REGISTERED) return;
    SensorEntry* s = findSensor(sensorID);
    if (!s || !s->acked) return;
    if (_debug) Serial.printf("[Node] sendFloat() - sensorID=%d value=%.2f\n", sensorID, value);
    storeSensorValue(*s, &value, sizeof(value));
    transmitSensor(*s);
}

// ─────────────────────────────────────────────────────────────────────────────
// loop
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::loop() {
    handleIncoming();

    switch (_state) {
        case STATE_UNREGISTERED: stepUnregistered(); break;
        case STATE_REGISTERING:  stepRegistering();  break;
        case STATE_REGISTERED:   stepRegistered();   break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// State machine
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::stepUnregistered() {
    if (millis() - _lastPresentAttempt < PRESENT_INTERVAL) return;
    _lastPresentAttempt = millis();
    sendNodePresent();
}

void LoRaNode::stepRegistering() {
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (!_sensors[i].acked) {
            if (millis() - _lastSensorAttempt < PRESENT_INTERVAL) return;
            _lastSensorAttempt = millis();
            sendSensorPresent(i);
            return;
        }
    }
    if (_debug) Serial.println("[Node] stepRegistering() - all sensors acked -> REGISTERED");
    _state          = STATE_REGISTERED;
    _justRegistered = true;
}

void LoRaNode::stepRegistered() {
    // Initial burst : send all sensors that have a value
    if (_justRegistered) {
        _justRegistered = false;
        if (_debug) Serial.println("[Node] stepRegistered() - initial burst: sending all sensor values");
        // Send heartbeat first so gateway gets battery level immediately
        if (_enableHeartbeat) sendHeartbeat();
        for (uint8_t i = 0; i < _sensorCount; i++) {
            if (_sensors[i].acked && _sensors[i].hasValue) {
                transmitSensor(_sensors[i]);
            }
        }
    }

    // Periodic / on-change auto-send
    checkAutoSend();

    // Heartbeat
    if (_enableHeartbeat && millis() - _lastHeartbeat >= _heartbeatInterval) {
        _lastHeartbeat = millis();
        sendHeartbeat();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Auto-send logic
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::checkAutoSend() {
    // On-change sensors (interval==0) are handled by sendXxx / handleActuator
    // Interval sensors : send if enough time has passed
    for (uint8_t i = 0; i < _sensorCount; i++) {
        SensorEntry& s = _sensors[i];
        if (!s.acked)         continue;
        if (!s.hasValue)      continue;
        if (s.sendInterval == 0) continue;   // on-change only

        uint32_t intervalMs = s.sendInterval * 1000UL;
        if (millis() - s.lastSent >= intervalMs) {
            transmitSensor(s);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame builders
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::sendNodePresent() {
    if (_debug) Serial.printf("[Node] sendNodePresent() - id=0x%08X name='%s'\n", _nodeID, _nodeName);
    PayloadNodePresent p;
    p.nodeID = _nodeID;
    strncpy(p.name, _nodeName, NAME_LEN);
    p.name[NAME_LEN] = '\0';
    strncpy(p.boardName, LORADOMO_BOARD_NAME, NAME_LEN);
    p.boardName[NAME_LEN] = '\0';
    sendFrame(MSG_NODE_PRESENT, 0, &p, sizeof(p), false);
}

void LoRaNode::sendSensorPresent(uint8_t idx) {
    if (_debug) Serial.printf("[Node] sendSensorPresent() - sensorID=%d name='%s' type=%d\n",
                  _sensors[idx].id, _sensors[idx].name, _sensors[idx].type);
    PayloadSensorPresent p;
    p.nodeID   = _nodeID;
    p.sensorID = _sensors[idx].id;
    p.dataType = (uint8_t)_sensors[idx].type;
    strncpy(p.name, _sensors[idx].name, NAME_LEN);
    p.name[NAME_LEN] = '\0';
    sendFrame(MSG_SENSOR_PRESENT, 0, &p, sizeof(p), false);
}

// ─────────────────────────────────────────────────────────────────────────────
// setBatteryCallback — register user callback for battery status
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::setBatteryCallback(BatteryReadCallback cb) {
    _batteryCallback = cb;
}

void LoRaNode::sendHeartbeat() {
    if (_batteryCallback) _batteryCallback(_isUSB, _battery);
    if (_debug) Serial.printf("[Node] sendHeartbeat() - battery=%d%% isUSB=%d uptime=%lums\n",
                  _battery, _isUSB, millis());
    PayloadHeartbeat p;
    p.nodeID  = _nodeID;
    p.battery = _battery;
    p.isUSB   = _isUSB ? 1 : 0;
    p.uptime  = millis();
    sendFrame(MSG_HEARTBEAT, 0, &p, sizeof(p), false);
}

// ─────────────────────────────────────────────────────────────────────────────
// transmitSensor — build and send MSG_SENSOR from stored value
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::transmitSensor(SensorEntry& s) {
    // If a read callback is registered, fetch fresh value before sending
    if (s.readCallback) {
        switch (s.type) {
            case TYPE_INT8: {
                int8_t v = ((ReadCallbackInt8)s.readCallback)();
                storeSensorValue(s, &v, sizeof(v));
                if (_debug) Serial.printf("[Node] transmitSensor() - readCallback INT8 sensorID=%d value=%d\n", s.id, v);
                break;
            }
            case TYPE_INT32: {
                int32_t v = ((ReadCallbackInt32)s.readCallback)();
                storeSensorValue(s, &v, sizeof(v));
                if (_debug) Serial.printf("[Node] transmitSensor() - readCallback INT32 sensorID=%d value=%ld\n", s.id, (long)v);
                break;
            }
            case TYPE_FLOAT: {
                float v = ((ReadCallbackFloat)s.readCallback)();
                storeSensorValue(s, &v, sizeof(v));
                if (_debug) Serial.printf("[Node] transmitSensor() - readCallback FLOAT sensorID=%d value=%.2f\n", s.id, v);
                break;
            }
        }
    }

    PayloadSensor p;
    p.nodeID   = _nodeID;
    p.sensorID = s.id;
    p.dataType = (uint8_t)s.type;
    p.value    = s.lastValue;

    if (_debug) Serial.printf("[Node] transmitSensor() - sensorID=%d name='%s'\n", s.id, s.name);
    sendFrame(MSG_SENSOR, 0, &p, sizeof(p), false);
    s.lastSent = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
// ACK actuator
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::sendAckActuator(uint32_t nodeID, uint8_t sensorID) {
    if (_debug) Serial.printf("[Node] sendAckActuator() - sensorID=%d\n", sensorID);
    PayloadAckActuator p;
    p.nodeID   = nodeID;
    p.sensorID = sensorID;
    sendFrame(MSG_ACK_ACTUATOR, 0, &p, sizeof(p), false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Incoming frame dispatcher
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::handleIncoming() {
    if (!radio.available()) return;
    if (_debug) Serial.println("[Node] handleIncoming() - frame received");

    uint8_t buf[64];
    uint8_t len = sizeof(buf);
    if (!radio.recv(buf, &len)) return;
    if (len < sizeof(FrameHeader)) return;

    FrameHeader h;
    memcpy(&h, buf, sizeof(h));

    if (h.key    != _key)    return;
    if (h.nodeID == _nodeID) return;

    const uint8_t* payload    = buf + sizeof(FrameHeader);
    const uint8_t  payloadLen = len - sizeof(FrameHeader);

    switch (h.type) {
        case MSG_ACK_NODE:    handleAckNode   (payload, payloadLen); break;
        case MSG_ACK_SENSOR:  handleAckSensor (payload, payloadLen); break;
        case MSG_ACTUATOR:    handleActuator  (payload, payloadLen); break;
        case MSG_REBOOT:      handleReboot    (payload, payloadLen); break;
        case MSG_REQUEST_REFRESH: handleRequestRefresh(); break;
        default: break;
    }
}

void LoRaNode::handleAckNode(const uint8_t* payload, uint8_t len) {
    if (_debug) Serial.println("[Node] handleAckNode() - ACK_NODE received");
    if (len < sizeof(PayloadAckNode)) {
        if (_debug) Serial.println("[Node] handleAckNode() - payload too short, ignored");
        return;
    }
    PayloadAckNode p;
    memcpy(&p, payload, sizeof(p));

    if (p.nodeID != _nodeID) {
        if (_debug) Serial.printf("[Node] handleAckNode() - nodeID 0x%08X != mine, ignored\n", p.nodeID);
        return;
    }
    if (_state != STATE_UNREGISTERED) {
        if (_debug) Serial.println("[Node] handleAckNode() - not in UNREGISTERED state, ignored");
        return;
    }

    if (_debug) Serial.println("[Node] handleAckNode() - node registered -> REGISTERING");
    _state             = STATE_REGISTERING;
    _lastSensorAttempt = 0;
    _currentSensorIdx  = 0;
}

void LoRaNode::handleAckSensor(const uint8_t* payload, uint8_t len) {
    if (_debug) Serial.println("[Node] handleAckSensor() - ACK_SENSOR received");
    if (len < sizeof(PayloadAckSensor)) {
        if (_debug) Serial.println("[Node] handleAckSensor() - payload too short, ignored");
        return;
    }
    PayloadAckSensor p;
    memcpy(&p, payload, sizeof(p));

    if (p.nodeID != _nodeID) {
        if (_debug) Serial.printf("[Node] handleAckSensor() - nodeID 0x%08X != mine, ignored\n", p.nodeID);
        return;
    }
    if (_state != STATE_REGISTERING) {
        if (_debug) Serial.println("[Node] handleAckSensor() - not in REGISTERING state, ignored");
        return;
    }

    SensorEntry* s = findSensor(p.sensorID);
    if (s) {
        s->acked = true;
        if (_debug) Serial.printf("[Node] handleAckSensor() - sensor %d '%s' acked\n", s->id, s->name);
    } else {
        if (_debug) Serial.printf("[Node] handleAckSensor() - sensorID %d not found\n", p.sensorID);
    }
}

void LoRaNode::handleActuator(const uint8_t* payload, uint8_t len) {
    if (_debug) Serial.println("[Node] handleActuator() - MSG_ACTUATOR received");
    if (len < sizeof(PayloadActuator)) {
        if (_debug) Serial.println("[Node] handleActuator() - payload too short, ignored");
        return;
    }

    PayloadActuator p;
    memcpy(&p, payload, sizeof(p));

    if (p.nodeID != _nodeID) {
        if (_debug) Serial.printf("[Node] handleActuator() - nodeID 0x%08X != mine, ignored\n", p.nodeID);
        return;
    }

    SensorEntry* s = findSensor(p.sensorID);
    if (!s) {
        if (_debug) Serial.printf("[Node] handleActuator() - sensorID %d not found\n", p.sensorID);
        return;
    }

    // Store new value
    storeSensorValue(*s, &p.value, sizeof(p.value));

    // ACK before callback
    sendAckActuator(_nodeID, p.sensorID);

    if (!s->actCallback) {
        if (_debug) Serial.printf("[Node] handleActuator() - sensorID %d no callback\n", p.sensorID);
        return;
    }

    switch ((DataType)p.dataType) {
        case TYPE_INT8:
            if (_debug) Serial.printf("[Node] handleActuator() - callback INT8 sensorID=%d value=%d\n",
                          p.sensorID, p.value.asInt8);
            ((CallbackInt8)s->actCallback)(p.sensorID, p.value.asInt8);
            break;
        case TYPE_INT32:
            if (_debug) Serial.printf("[Node] handleActuator() - callback INT32 sensorID=%d value=%ld\n",
                          p.sensorID, (long)p.value.asInt32);
            ((CallbackInt32)s->actCallback)(p.sensorID, p.value.asInt32);
            break;
        case TYPE_FLOAT:
            if (_debug) Serial.printf("[Node] handleActuator() - callback FLOAT sensorID=%d value=%.2f\n",
                          p.sensorID, p.value.asFloat);
            ((CallbackFloat)s->actCallback)(p.sensorID, p.value.asFloat);
            break;
        default:
            if (_debug) Serial.printf("[Node] handleActuator() - unknown dataType %d\n", p.dataType);
            break;
    }
}

void LoRaNode::handleReboot(const uint8_t* payload, uint8_t len) {
    if (_debug) Serial.println("[Node] handleReboot() - reboot command received");
    if (len < sizeof(PayloadReboot)) return;
    PayloadReboot p;
    memcpy(&p, payload, sizeof(p));
    if (p.nodeID != _nodeID) {
        if (_debug) Serial.printf("[Node] handleReboot() - nodeID 0x%08X != mine, ignored\n", p.nodeID);
        return;
    }
    if (_debug) Serial.println("[Node] handleReboot() - rebooting in 200ms");
    delay(200);
    ESP.restart();
}

// ─────────────────────────────────────────────────────────────────────────────
// Gateway boot handler — reset to UNREGISTERED so node re-registers
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::handleGatewayBoot() {
    if (_debug) Serial.println("[Node] handleGatewayBoot() - gateway rebooted, resetting registration");

    // Reset all sensors ack flags
    for (uint8_t i = 0; i < _sensorCount; i++) {
        _sensors[i].acked = false;
    }

    _state              = STATE_UNREGISTERED;
    _lastPresentAttempt = 0;
    _lastSensorAttempt  = 0;
    _currentSensorIdx   = 0;
    _justRegistered     = false;
}

void LoRaNode::handleRequestRefresh() {
    if (_debug) Serial.println("[Node] handleRequestRefresh() - sending heartbeat + all sensor values");

    if (_state != STATE_REGISTERED) {
        if (_debug) Serial.println("[Node] handleRequestRefresh() - not registered, ignored");
        return;
    }

    // Send heartbeat first for battery level
    if (_enableHeartbeat) sendHeartbeat();

    // Send current values of all sensors that have a value
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (_sensors[i].acked && _sensors[i].hasValue) {
            transmitSensor(_sensors[i]);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Low-level frame send
// ─────────────────────────────────────────────────────────────────────────────
void LoRaNode::sendFrame(uint8_t type, uint32_t destID,
                          const void* payload, uint8_t payloadSize,
                          bool ackReq) {
    if (_debug) Serial.printf("[Node] sendFrame() - type=0x%02X dest=0x%08X size=%d ackReq=%d\n",
                  type, destID, payloadSize, ackReq);
    if (destID == 0) ackReq = false;

    uint8_t buffer[64];
    if (sizeof(FrameHeader) + payloadSize > sizeof(buffer)) return;

    FrameHeader h;
    h.version   = 1;
    h.type      = type;
    h.nodeID    = _nodeID;
    h.messageID = _msgID++;
    h.flags     = ackReq ? FLAG_ACK_REQ : 0;
    h.key       = _key;

    memcpy(buffer, &h, sizeof(h));
    if (payload && payloadSize) memcpy(buffer + sizeof(h), payload, payloadSize);

    for (int i = 0; i < RETRY_MAX; i++) {
        delay(random(20, 80));
        radio.send(buffer, sizeof(h) + payloadSize);
        radio.waitPacketSent();
        if (!ackReq) return;
        if (waitAck(h.messageID)) return;
    }
}

bool LoRaNode::waitAck(uint8_t id) {
    uint32_t start = millis();
    while (millis() - start < (_latency * 2 + 50)) {
        if (radio.available()) {
            uint8_t buf[32];
            uint8_t len = sizeof(buf);
            radio.recv(buf, &len);
            if (len < sizeof(FrameHeader)) continue;
            FrameHeader h;
            memcpy(&h, buf, sizeof(h));
            if (h.type == MSG_ACK && h.messageID == id) {
                _latency = (_latency + (millis() - start)) / 2;
                return true;
            }
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
LoRaNode::SensorEntry* LoRaNode::findSensor(uint8_t id) {
    for (uint8_t i = 0; i < _sensorCount; i++)
        if (_sensors[i].id == id) return &_sensors[i];
    return nullptr;
}

void LoRaNode::storeSensorValue(SensorEntry& s, const void* rawValue, uint8_t valSize) {
    memset(&s.lastValue, 0, sizeof(s.lastValue));
    memcpy(&s.lastValue, rawValue, valSize);
    s.hasValue = true;
}

bool LoRaNode::valuesEqual(const SensorEntry& s, const void* rawValue, uint8_t valSize) {
    return memcmp(&s.lastValue, rawValue, valSize) == 0;
}