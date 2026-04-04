// ================= LoRaNode.h =================
#pragma once
#include "LoRaTypes.h"
#include <RH_SX126x.h>

// --- Heltec V3 / SX1262 pins ───────────────────────────────────────────────
#define LORA_CS    8
#define LORA_DIO1  14
#define LORA_BUSY  13
#define LORA_RST   12
#define LORA_FREQ  868.0f   // MHz - Europe
#define LORA_TX_DB 13       // dBm

// --- Actuator callback types
// Called when the gateway sends a value to the node. Pass nullptr if not needed.
typedef void (*CallbackInt8 )(uint8_t sensorID, int8_t  value);
typedef void (*CallbackInt32)(uint8_t sensorID, int32_t value);
typedef void (*CallbackFloat)(uint8_t sensorID, float   value);

// --- Read callback types
// Called just before each send to fetch the current sensor value.
// Return type must match the sensor DataType. Pass nullptr to skip.
typedef int8_t  (*ReadCallbackInt8 )();
typedef int32_t (*ReadCallbackInt32)();
typedef float   (*ReadCallbackFloat)();

class LoRaNode {
public:

    void begin(const char* key, const char* nodeName, bool debug = false);
    void loop();

    // -- Sensor registration ─────────────────────────────────────────────────
    // sendInterval : seconds between automatic sends (0 = on change only)
    // callback     : nullptr if sensor never receives values from gateway
    bool addSensor(uint8_t sensorID, DataType type, const char* name,
                   uint32_t sendInterval    = 0,
                   void*    actCallback     = nullptr,
                   void*    readCallback    = nullptr);

    // -- Pre-load value without sending (e.g. from EEPROM in setup()) ────────
    void setSensorInt8 (uint8_t sensorID, int8_t  value);
    void setSensorInt32(uint8_t sensorID, int32_t value);
    void setSensorFloat(uint8_t sensorID, float   value);

    // -- Manual send (immediate, resets interval timer) ───────────────────────
    // Only works once node is REGISTERED.
    void sendInt8 (uint8_t sensorID, int8_t  value);
    void sendInt32(uint8_t sensorID, int32_t value);
    void sendFloat(uint8_t sensorID, float   value);

protected:

    void sendFrame(uint8_t type, uint32_t destID,
                   const void* payload, uint8_t payloadSize,
                   bool ackReq);

    bool waitAck(uint8_t msgID);
    static uint16_t hashKey(const char* key);

    RH_SX126x radio{LORA_CS, LORA_DIO1, LORA_BUSY, LORA_RST};

    uint32_t _nodeID  = 0;
    uint16_t _key     = 0;
    uint8_t  _msgID   = 0;
    uint32_t _latency = 200;

    bool     _enableHeartbeat   = true;
    uint32_t _heartbeatInterval = 120000UL;
    uint32_t _lastHeartbeat     = 0;

    uint8_t  _battery = 100;
    bool     _debug   = false;

private:

    enum NodeState : uint8_t {
        STATE_UNREGISTERED,
        STATE_REGISTERING,
        STATE_REGISTERED
    };

    NodeState _state = STATE_UNREGISTERED;
    char      _nodeName[NAME_LEN + 1] = {};

    // -- Sensor table ────────────────────────────────────────────────────────
    struct SensorEntry {
        uint8_t     id                 = 0;
        DataType    type               = TYPE_FLOAT;
        char        name[NAME_LEN + 1] = {};
        bool        acked              = false;
        void*       actCallback        = nullptr;  // actuator callback, typed via DataType
        void*       readCallback       = nullptr;  // read callback, typed via DataType

        SensorValue lastValue          = {};
        bool        hasValue           = false;

        uint32_t    sendInterval       = 0;      // seconds, 0 = on change only
        uint32_t    lastSent           = 0;      // millis() of last send
    };

    SensorEntry _sensors[MAX_SENSORS];
    uint8_t     _sensorCount = 0;

    uint32_t _lastPresentAttempt = 0;
    uint32_t _lastSensorAttempt  = 0;
    uint8_t  _currentSensorIdx   = 0;

    bool _justRegistered = false;   // flag to trigger initial send burst

    // -- State machine ───────────────────────────────────────────────────────
    void stepUnregistered();
    void stepRegistering();
    void stepRegistered();

    void sendNodePresent();
    void sendSensorPresent(uint8_t idx);
    void sendHeartbeat();

    // -- Auto-send logic ─────────────────────────────────────────────────────
    void checkAutoSend();

    // -- Incoming frames ─────────────────────────────────────────────────────
    void handleIncoming();
    void handleAckNode    (const uint8_t* payload, uint8_t len);
    void handleAckSensor  (const uint8_t* payload, uint8_t len);
    void handleActuator   (const uint8_t* payload, uint8_t len);
    void handleReboot     (const uint8_t* payload, uint8_t len);
    void handleGatewayBoot();

    // -- Helpers ─────────────────────────────────────────────────────────────
    SensorEntry* findSensor(uint8_t id);
    void         storeSensorValue(SensorEntry& s, const void* rawValue, uint8_t valSize);
    bool         valuesEqual(const SensorEntry& s, const void* rawValue, uint8_t valSize);
    void         transmitSensor(SensorEntry& s);
    void         sendAckActuator(uint32_t nodeID, uint8_t sensorID);
};
