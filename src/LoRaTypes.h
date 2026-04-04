// ================= LoRaTypes.h =================
#pragma once
#include <Arduino.h>

// --- Limits ────────────────────────────────────────────────────────────────
#define MAX_NODES               10
#define MAX_SENSORS             10
#define MAX_PENDING_ACT          5       // max actuator messages awaiting ACK
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
    MSG_GATEWAY_BOOT    = 0x0B    // gateway broadcast on startup: node resets
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
    uint32_t uptime;     // ms
};

struct __attribute__((packed)) PayloadReboot {
    uint32_t nodeID;
};
