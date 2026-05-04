// ================= LoRaNode.h =================
#pragma once
#include "LoRaTypes.h"
#include "boards.h"

#if defined(LORADOMO_USE_SX1276)
  #include <RH_RF95.h>
#else
  #include <RH_SX126x.h>
#endif
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

// --- Battery read callback
// Called automatically before each heartbeat.
// Set isUSB=true if powered by USB, and battery=0..100 (%).
// If not registered, defaults are used: isUSB=true, battery=100.
//
// LiPo voltage curve:  3.0V = 0%,  4.2V = 100%
// USB detection:       voltage > 4.3V → USB (charger IC raises VBAT above LiPo max)
//
// Helper macro for % calculation (use in your callback):
//   float pct = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;
//   if (pct < 0.0f)   pct = 0.0f;
//   if (pct > 100.0f) pct = 100.0f;
//   battery = (uint8_t)pct;
//
// ── Compatible V2 / V3 / V4 (utilise #ifdef pour le pin de contrôle) ─────────
// V2  — ADC pin GPIO13, pas de pin de contrôle
// V3/V4 — ADC pin GPIO1, pin de contrôle GPIO37 (LORA_BAT_CTRL_PIN)
//
//   void myBattery(bool& isUSB, uint8_t& battery) {
//   #ifdef LORA_BAT_CTRL_PIN
//       pinMode(LORA_BAT_CTRL_PIN, OUTPUT);
//       digitalWrite(LORA_BAT_CTRL_PIN, LOW);  delay(5);
//   #endif
//       float adc = (analogRead(LORA_BAT_PIN) / 4095.0f) * LORA_BAT_VREF;
//       float v   = adc * LORA_BAT_DIV;
//   #ifdef LORA_BAT_CTRL_PIN
//       digitalWrite(LORA_BAT_CTRL_PIN, HIGH);
//   #endif
//       isUSB = (v > 4.3f);
//       if (isUSB) { battery = 100; return; }
//       float pct = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;
//       if (pct < 0.0f) pct = 0.0f; if (pct > 100.0f) pct = 100.0f;
//       battery = (uint8_t)pct;
//   }
//
//   // Registration (call before loop()):
//   node.setBatteryCallback(myBattery);
//
typedef void (*BatteryReadCallback)(bool& isUSB, uint8_t& battery);

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

    // -- Battery status callback (optional) ─────────────────────────────────
    // Register a function called automatically before each heartbeat.
    // See BatteryReadCallback above for usage and board-specific examples.
    void setBatteryCallback(BatteryReadCallback cb);

    // -- Radio configuration (call after begin(), before first loop())
    void setFrequency  (float freq);                    // MHz, e.g. 868.0, 915.0, 433.0
    void setTxPower    (int   dbm);                     // dBm
    void setModemConfig(LoRaModemConfig config);        // see LoRaTypes.h

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

#if defined(LORADOMO_USE_SX1276)
    RH_RF95  radio{LORA_CS, LORA_DIO0};
#else
    RH_SX126x radio{LORA_CS, LORA_DIO1, LORA_BUSY, LORA_RST};
#endif

    uint32_t _nodeID  = 0;
    uint16_t _key     = 0;
    uint8_t  _msgID   = 0;
    uint32_t _latency = 200;

    bool     _enableHeartbeat   = true;
    uint32_t _heartbeatInterval = 120000UL;
    uint32_t _lastHeartbeat     = 0;

    uint8_t        _battery     = 100;
    bool           _isUSB       = true;
    bool           _debug       = false;
    float          _frequency   = LORA_FREQ;
    int            _txPower     = LORA_TX_DB;
    LoRaModemConfig _modemConfig = MODEM_BW125_CR45_SF128;

    // -- Node state (accessible by LoRaGateway for local sensor init) ────────
    enum NodeState : uint8_t {
        STATE_UNREGISTERED,
        STATE_REGISTERING,
        STATE_REGISTERED
    };
    NodeState _state = STATE_UNREGISTERED;

    // -- Sensor table (accessible by LoRaGateway for local sensors) ──────────
    struct SensorEntry {
        uint8_t     id                 = 0;
        DataType    type               = TYPE_FLOAT;
        char        name[NAME_LEN + 1] = {};
        bool        acked              = false;
        void*       actCallback        = nullptr;
        void*       readCallback       = nullptr;
        SensorValue lastValue          = {};
        bool        hasValue           = false;
        uint32_t    sendInterval       = 0;
        uint32_t    lastSent           = 0;
    };

    SensorEntry _sensors[MAX_SENSORS];
    uint8_t     _sensorCount = 0;

    SensorEntry* findSensor(uint8_t id);
    void         storeSensorValue(SensorEntry& s, const void* rawValue, uint8_t valSize);
    bool         valuesEqual(const SensorEntry& s, const void* rawValue, uint8_t valSize);

    // Virtual: LoRaGateway overrides this to publish via MQTT instead of LoRa
    virtual void transmitSensor(SensorEntry& s);

private:

    char      _nodeName[NAME_LEN + 1] = {};

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
    void handleRequestRefresh();

    // -- Internal radio helpers
    void applyRadioConfig();
    void applyModemConfig();

    void sendAckActuator(uint32_t nodeID, uint8_t sensorID);

    BatteryReadCallback _batteryCallback = nullptr;
};