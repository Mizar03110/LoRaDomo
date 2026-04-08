// ================= LoRaTypes.h =================
#pragma once
#include <Arduino.h>

// --- Limits ────────────────────────────────────────────────────────────────
#define MAX_NODES               10
#define MAX_SENSORS             10
#define MAX_PENDING_ACT          5       // max actuator messages awaiting ACK
#define MAX_MQTT_QUEUE          16       // max MQTT messages queued for deferred publish
#define RETRY_MAX                3
#define NAME_LEN                15       // max chars for node/sensor name (+ null)
#define PRESENT_INTERVAL      5000UL    // retry interval for presentation frames (ms)
#define ACTUATOR_RETRY_INTERVAL 5000UL  // retry interval for actuator messages (ms)

// --- Frame flags ───────────────────────────────────────────────────────────
#define FLAG_ACK_REQ  0x01

// --- Message types ─────────────────────────────────────────────────────────
enum MessageType : uint8_t {
    MSG_NODE_PRESENT    = 0x01,
    MSG_ACK_NODE        = 0x02,
    MSG_SENSOR_PRESENT  = 0x03,
    MSG_ACK_SENSOR      = 0x04,
    MSG_SENSOR          = 0x05,
    MSG_HEARTBEAT       = 0x06,
    MSG_ACK             = 0x07,   // generic ACK (internal use)
    MSG_REBOOT          = 0x08,
    MSG_ACTUATOR        = 0x09,   // gateway → node : set a sensor/actuator value
    MSG_ACK_ACTUATOR    = 0x0A,   // node → gateway : ACK for MSG_ACTUATOR
    MSG_REQUEST_REFRESH = 0x0B    // gateway broadcast: request all sensors to send their current values
};

// --- LoRa modem configurations
enum LoRaModemConfig : uint8_t {
    MODEM_BW125_CR45_SF128  = 0,  // BW 125kHz CR 4/5 SF7  — default, balanced range/speed
    MODEM_BW500_CR45_SF128  = 1,  // BW 500kHz CR 4/5 SF7  — fast, shorter range
    MODEM_BW31_CR48_SF512   = 2,  // BW 31kHz  CR 4/8 SF9  — slow, longer range
    MODEM_BW125_CR48_SF4096 = 3   // BW 125kHz CR 4/8 SF12 — max range, very slow
};

// --- Sensor data types ─────────────────────────────────────────────────────
enum DataType : uint8_t {
    TYPE_INT8  = 0,   // int8_t  (1B) — small signed integers, humidity, %
    TYPE_INT32 = 1,   // int32_t (4B)
    TYPE_FLOAT = 2    // float   (4B)
};

// --- Sensor value union ─────────────────────────────────────────────────────
union SensorValue {
    int8_t  asInt8;
    int32_t asInt32;
    float   asFloat;
};

// --- Frame header (every frame starts with this) ───────────────────────────
struct __attribute__((packed)) FrameHeader {
    uint8_t  version   = 1;
    uint8_t  type;
    uint32_t nodeID;
    uint8_t  messageID;
    uint8_t  flags;
    uint16_t key;
};

// --- Payloads ──────────────────────────────────────────────────────────────

struct __attribute__((packed)) PayloadNodePresent {
    uint32_t nodeID;
    char     name[NAME_LEN + 1];
    char     boardName[NAME_LEN + 1];  // e.g. "Heltec V3"
};

struct __attribute__((packed)) PayloadAckNode {
    uint32_t nodeID;
};

struct __attribute__((packed)) PayloadSensorPresent {
    uint32_t nodeID;
    uint8_t  sensorID;
    uint8_t  dataType;
    char     name[NAME_LEN + 1];
};

struct __attribute__((packed)) PayloadAckSensor {
    uint32_t nodeID;
    uint8_t  sensorID;
};

struct __attribute__((packed)) PayloadSensor {
    uint32_t    nodeID;
    uint8_t     sensorID;
    uint8_t     dataType;
    SensorValue value;
};

struct __attribute__((packed)) PayloadActuator {
    uint32_t    nodeID;
    uint8_t     sensorID;
    uint8_t     dataType;
    SensorValue value;
};
 
struct __attribute__((packed)) PayloadAckActuator {
    uint32_t nodeID;
    uint8_t  sensorID;
};

struct __attribute__((packed)) PayloadHeartbeat {
    uint32_t nodeID;
    uint8_t  battery;    // 0-100 %
    uint8_t  isUSB;      // 1 = USB powered, 0 = battery
    uint32_t uptime;     // ms
};

struct __attribute__((packed)) PayloadReboot {
    uint32_t nodeID;
};

struct __attribute__((packed)) PayloadRequestRefresh {
    uint32_t nodeID;  // 0xFFFFFFFF for broadcast
};