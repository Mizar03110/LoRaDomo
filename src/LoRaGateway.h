// ================= LoRaGateway.h =================
#pragma once

#include "LoRaNode.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>

class LoRaGateway : public LoRaNode {

public:
    void begin(const char* gatewayName,
               const char* key,
               const char* mqttServer,
               const char* mqttUser,
               const char* mqttPass,
               const char* ssid,
               const char* wifiPassword,
               bool        debug = false);

    void loop();

private:

    // ═══════════════════════════════════════════════════════════════════════
    // Network
    // ═══════════════════════════════════════════════════════════════════════
    WiFiClient       _espClient;
    PubSubClient     _mqtt{_espClient};
    WebServer        _http{80};
    WebSocketsServer _ws{81};

    const char*   _gatewayName;
    const char*   _mqttUser;
    const char*   _mqttPass;
    const char*   _ssid;
    const char*   _wifiPassword;

    unsigned long _lastWifiAttempt = 0;
    unsigned long _lastMqttAttempt = 0;
    const unsigned long _wifiRetryInterval = 5000UL;
    const unsigned long _mqttRetryInterval = 5000UL;

    bool _webStarted       = false;
    bool _debug            = false;
    bool _nvsDirty         = false;   // set true when registry needs saving
    bool _bootBroadcastDone = false;  // send MSG_GATEWAY_BOOT once from loop()
    unsigned long _lastWsPing = 0;
    const unsigned long _wsPingInterval = 2000UL;

    void connectWiFi();
    void sendWsPing();
    void connectMQTT();

    // ═══════════════════════════════════════════════════════════════════════
    // Node / Sensor registry
    // ═══════════════════════════════════════════════════════════════════════
    struct SensorInfo {
        uint8_t     id        = 0;
        uint8_t     dataType  = TYPE_FLOAT;
        char        name[NAME_LEN + 1] = {};
        SensorValue lastValue = {};
        bool        hasValue  = false;
    };

    struct NodeInfo {
        uint32_t id           = 0;
        char     name[NAME_LEN + 1] = {};
        int      rssi         = 0;
        float    snr          = 0.0f;
        uint32_t latency      = 0;
        uint8_t  battery      = 0;
        uint32_t uptime       = 0;
        uint32_t lastSeen     = 0;
        bool     online       = false;
        char     onlineDuration[24]  = {};  // formatted duration string e.g. "2d 3h 15min"
        char     offlineDuration[24] = {};
        uint32_t offlineAt           = 0;   // millis() when node went offline

        SensorInfo sensors[MAX_SENSORS];
        uint8_t    sensorCount = 0;
    };

    NodeInfo _nodes[MAX_NODES];

    NodeInfo*   getNode       (uint32_t id);
    NodeInfo*   getOrAddNode  (uint32_t id);
    SensorInfo* getSensor     (NodeInfo* n, uint8_t sensorID);
    SensorInfo* getOrAddSensor(NodeInfo* n, uint8_t sensorID);
    void        removeNode    (uint32_t id);

    void updateNodeStats(uint32_t id, int rssi, float snr);
    void checkTimeouts();
    void updateOfflineDurations();

    // ═══════════════════════════════════════════════════════════════════════
    // Actuator pending queue
    // Stores MSG_ACTUATOR messages awaiting MSG_ACK_ACTUATOR from the node.
    // Up to MAX_PENDING_ACT entries, 3 retries at ACTUATOR_RETRY_INTERVAL.
    // ═══════════════════════════════════════════════════════════════════════
    struct PendingActuator {
        bool         active    = false;
        uint32_t     nodeID    = 0;
        uint8_t      sensorID  = 0;
        uint8_t      dataType  = 0;
        SensorValue  value     = {};
        uint8_t      retries   = 0;        // attempts sent so far
        uint32_t     lastSent  = 0;        // millis() of last attempt
    };

    PendingActuator _pendingAct[MAX_PENDING_ACT];

    // Add new entry to queue and send first attempt immediately
    void queueActuator(uint32_t nodeID, uint8_t sensorID,
                       uint8_t dataType, SensorValue value);

    // Called from loop() - resend or expire pending entries
    void checkPendingActuators();

    // Send a single MSG_ACTUATOR frame
    void sendActuator(const PendingActuator& a);

    // Called when MSG_ACK_ACTUATOR is received
    void handleAckActuator(const uint8_t* payload, uint8_t len);

    // Publish NACK on OUT topic after all retries failed
    void publishNack(uint32_t nodeID, uint8_t sensorID);

    // ═══════════════════════════════════════════════════════════════════════
    // LoRa frame handling
    // ═══════════════════════════════════════════════════════════════════════
    void handleLoRa();
    void handleNodePresent  (const uint8_t* payload, uint8_t len, int rssi, float snr);
    void handleSensorPresent(const uint8_t* payload, uint8_t len);
    void handleSensor       (const uint8_t* payload, uint8_t len, int rssi, float snr);
    void handleHeartbeat    (const uint8_t* payload, uint8_t len);

    void sendAckNode  (uint32_t nodeID);
    void sendAckSensor(uint32_t nodeID, uint8_t sensorID);
    void sendReboot   (uint32_t nodeID);

    // ═══════════════════════════════════════════════════════════════════════
    // MQTT
    // ═══════════════════════════════════════════════════════════════════════
    void publishNodeStatus (const NodeInfo& n, const char* status);
    void publishSensorValue(const NodeInfo& n, const SensorInfo& s);

    // Parse incoming MQTT IN message and route to node via LoRa
    void handleMqttIn(const char* topic, const uint8_t* payload, unsigned int len);

    // Static trampoline needed by PubSubClient callback
    static void mqttCallback(char* topic, uint8_t* payload,
                             unsigned int length, void* self);

    // ═══════════════════════════════════════════════════════════════════════
    // Web server / WebSocket
    // ═══════════════════════════════════════════════════════════════════════
    // ═══════════════════════════════════════════════════════════════════════
    // Persistence (NVS via Preferences)
    // ═══════════════════════════════════════════════════════════════════════
    Preferences _prefs;
    void loadFromNVS();
    void saveToNVS();
    void sendGatewayBoot();

    void setupWebServer();
    void handleWsEvent(uint8_t num, WStype_t type,
                       uint8_t* payload, size_t length);
    String buildStateJson();
    void   broadcastState();
    void   handleHttpRoot();
};
