# API Reference

## LoRaNode

Include with `#include "LoRaNode.h"`.

---

### `begin()`

```cpp
void begin(const char* key, const char* nodeName, bool debug = false);
```

Initializes the radio and starts the registration state machine.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `const char*` | Shared network key. Must match the gateway key. The string is hashed internally — any length is accepted. |
| `nodeName` | `const char*` | Human-readable node name, max 15 characters. Used as MQTT topic segment and displayed in the web UI. |
| `debug` | `bool` | Enable serial debug output. Default `false`. |

Call once in `setup()`, before `addSensor()`.

---

### `addSensor()`

```cpp
bool addSensor(uint8_t      sensorID,
               DataType     type,
               const char*  name,
               uint32_t     sendInterval = 0,
               void*        actCallback  = nullptr,
               void*        readCallback = nullptr);
```

Registers a sensor with the node. Must be called in `setup()` after `begin()`, before the first `loop()`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `sensorID` | `uint8_t` | Unique sensor identifier within this node (1–255). |
| `type` | `DataType` | Data type: `TYPE_INT8`, `TYPE_INT32`, or `TYPE_FLOAT`. |
| `name` | `const char*` | Sensor name, max 15 characters. Used as MQTT topic segment. |
| `sendInterval` | `uint32_t` | Automatic send interval in **seconds**. `0` = send on change only. Default `0`. |
| `actCallback` | `void*` | Actuator callback (cast to `void*`). Called when gateway sends a value. `nullptr` if not needed. |
| `readCallback` | `void*` | Read callback (cast to `void*`). Called just before each automatic send. `nullptr` if not needed. |

Returns `false` if the sensor table is full (`MAX_SENSORS = 10`).

**Callback signatures:**

```cpp
// Actuator callbacks — called when gateway sends a value
void myActInt8 (uint8_t sensorID, int8_t  value);
void myActInt32(uint8_t sensorID, int32_t value);
void myActFloat(uint8_t sensorID, float   value);

// Read callbacks — called before each send, return current value
int8_t  myReadInt8 ();
int32_t myReadInt32();
float   myReadFloat();
```

Pass them cast to `void*`:

```cpp
node.addSensor(1, TYPE_FLOAT, "temperature", 30, nullptr, (void*)myReadFloat);
node.addSensor(2, TYPE_INT8,  "relay",        0, (void*)myActInt8, nullptr);
```

---

### `setSensorInt8()` / `setSensorInt32()` / `setSensorFloat()`

```cpp
void setSensorInt8 (uint8_t sensorID, int8_t  value);
void setSensorInt32(uint8_t sensorID, int32_t value);
void setSensorFloat(uint8_t sensorID, float   value);
```

Pre-loads a sensor value **without sending it**. Use in `setup()` to restore values from EEPROM/Preferences before the node registers. The value will be sent automatically on the first `REGISTERED` burst.

---

### `sendInt8()` / `sendInt32()` / `sendFloat()`

```cpp
void sendInt8 (uint8_t sensorID, int8_t  value);
void sendInt32(uint8_t sensorID, int32_t value);
void sendFloat(uint8_t sensorID, float   value);
```

Sends a sensor value immediately. Only works when the node is in `REGISTERED` state — silently ignored otherwise. Also stores the value internally and resets the auto-send timer.

Typical use: call from an actuator callback to confirm a new state, or from user code when a value changes.

---

### `loop()`

```cpp
void loop();
```

Must be called on every iteration of the Arduino `loop()`. Handles:
- Incoming frame reception (ACKs, actuator commands, reboot, request refresh)
- Registration state machine (node present, sensor present retries)
- Auto-send for interval-based sensors
- Read callback invocation before sends
- Battery callback invocation before each heartbeat
- Heartbeat transmission

---

### `setBatteryCallback()`

```cpp
void setBatteryCallback(BatteryReadCallback cb);
```

Registers a function called automatically before each heartbeat to read the battery level and USB status.

```cpp
typedef void (*BatteryReadCallback)(bool& isUSB, uint8_t& battery);
```

| Parameter | Description |
|-----------|-------------|
| `isUSB` | Set to `true` if the node is USB-powered (charger IC raises VBAT above 4.3V). |
| `battery` | Battery level 0–100%. |

If not registered, defaults are used: `isUSB = true`, `battery = 100`.

**Board-compatible example** (works on V2, V3, V4, TTGO V1 — uses `#ifdef` for the control pin):

```cpp
void myBattery(bool& isUSB, uint8_t& battery) {
#ifdef LORA_BAT_CTRL_PIN
    pinMode(LORA_BAT_CTRL_PIN, OUTPUT);
    digitalWrite(LORA_BAT_CTRL_PIN, LOW);
    delay(5);
#endif
    float adc = (analogRead(LORA_BAT_PIN) / 4095.0f) * LORA_BAT_VREF;
    float v   = adc * LORA_BAT_DIV;
#ifdef LORA_BAT_CTRL_PIN
    digitalWrite(LORA_BAT_CTRL_PIN, HIGH);
#endif
    isUSB = (v > 4.3f);
    if (isUSB) { battery = 100; return; }
    float pct = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    battery = (uint8_t)pct;
}

// In setup(), after begin():
node.setBatteryCallback(myBattery);
```

---

### `setFrequency()` / `setTxPower()` / `setModemConfig()`

```cpp
void setFrequency  (float freq);
void setTxPower    (int   dbm);
void setModemConfig(LoRaModemConfig config);
```

Override the board defaults for frequency, TX power, and modem configuration. Call after `begin()`, before the first `loop()`. All nodes and the gateway must use the same settings.

```cpp
enum LoRaModemConfig : uint8_t {
    MODEM_BW125_CR45_SF128  = 0,  // default — balanced range/speed
    MODEM_BW500_CR45_SF128  = 1,  // fast, shorter range
    MODEM_BW31_CR48_SF512   = 2,  // slow, longer range
    MODEM_BW125_CR48_SF4096 = 3   // max range, very slow
};
```

---

### Protected members (accessible from subclasses)

| Member | Type | Description |
|--------|------|-------------|
| `_battery` | `uint8_t` | Battery level 0–100%. Overridden by `setBatteryCallback()` if registered. |
| `_isUSB` | `bool` | USB power status. Overridden by `setBatteryCallback()` if registered. |
| `_heartbeatInterval` | `uint32_t` | Heartbeat interval in ms. Default 120000 (2 minutes). |
| `_enableHeartbeat` | `bool` | Set to `false` to disable heartbeat. Default `true`. |

---

### DataType enum

```cpp
enum DataType : uint8_t {
    TYPE_INT8  = 0,   // int8_t  — range -128 to 127
    TYPE_INT32 = 1,   // int32_t — range -2147483648 to 2147483647
    TYPE_FLOAT = 2    // float   — 32-bit IEEE 754
};
```

---

## LoRaGateway

Include with `#include "LoRaGateway.h"`. Extends `LoRaNode`.

---

### `begin()`

```cpp
void begin(const char* gatewayName,
           const char* key,
           const char* mqttServer,
           const char* mqttUser,
           const char* mqttPass,
           const char* ssid,
           const char* wifiPassword,
           bool        debug = false);
```

Initializes the radio, WiFi, MQTT client, WebSocket server, and loads the node registry from NVS.

| Parameter | Type | Description |
|-----------|------|-------------|
| `gatewayName` | `const char*` | Gateway name. Used as MQTT topic prefix and displayed in the web UI. Max 15 characters. |
| `key` | `const char*` | Shared network key. Must match all nodes. |
| `mqttServer` | `const char*` | MQTT broker IP address or hostname. |
| `mqttUser` | `const char*` | MQTT username. |
| `mqttPass` | `const char*` | MQTT password. |
| `ssid` | `const char*` | WiFi network name. |
| `wifiPassword` | `const char*` | WiFi password. |
| `debug` | `bool` | Enable serial debug output. Default `false`. |

---

### `loop()`

```cpp
void loop();
```

Must be called on every iteration of the Arduino `loop()`. Handles:
- WiFi and MQTT reconnection (non-blocking)
- HTTP and WebSocket server
- LoRa frame reception and dispatching
- Actuator retry queue
- Node timeout detection
- Offline duration updates
- NVS save when registry changes

---

## MQTT Topics

All topics use the gateway name as prefix.

### Published by gateway (OUT)

| Topic | Value | Retained | Description |
|-------|-------|----------|-------------|
| `<gw>/OUT/<gw>/status` | `1` / `0` | Yes | Gateway online (1) or offline (0). `0` is the LWT — published automatically by broker if gateway disconnects. |
| `<gw>/OUT/<node>/status` | `1` / `0` | Yes | Node online (1) or offline (0). |
| `<gw>/OUT/<node>/<sensor>` | number | No | Latest sensor value as a numeric string. |
| `<gw>/OUT/<node>/battery` | `0`–`100` | No | Battery level in %, published on each heartbeat. |

### Subscribed by gateway (IN)

| Topic | Payload | Description |
|-------|---------|-------------|
| `<gw>/IN/<node>/<sensor>` | number | Send a value to a node sensor. The gateway looks up the node and sensor by name, converts the payload to the correct type, and transmits `MSG_ACTUATOR` to the node via LoRa. |

### Notes
- `<gw>` is the gateway name passed to `begin()`
- `<node>` is the node name passed to `begin()` on the node
- `<sensor>` is the sensor name passed to `addSensor()`
- All numeric values are published as plain text strings (e.g. `"21.50"`, `"58"`, `"1"`)

---

## Web UI

The gateway serves a single-page dashboard at `http://<gateway_ip>/`.

Real-time updates are delivered via WebSocket on port 81. The UI automatically reconnects if the gateway reboots.

### Displayed information per node
- Node name and online/offline status with duration
- Chip ID, RSSI, SNR, battery level
- Per sensor: name, ID, data type, last value

### Actions
- **Reboot**: sends `MSG_REBOOT` to the node via LoRa
- **Delete**: removes the node from the registry and NVS
- **Reboot gateway**: restarts the ESP32

---

## Constants (LoRaTypes.h)

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_NODES` | 10 | Maximum number of nodes the gateway can manage |
| `MAX_SENSORS` | 10 | Maximum sensors per node |
| `MAX_PENDING_ACT` | 5 | Maximum concurrent pending actuator messages |
| `RETRY_MAX` | 3 | Max retries for actuator delivery |
| `NAME_LEN` | 15 | Max length for node/sensor names |
| `PRESENT_INTERVAL` | 5000 ms | Retry interval for registration frames |
| `ACTUATOR_RETRY_INTERVAL` | 5000 ms | Retry interval for actuator delivery |

---

## Board Pin Constants (boards.h)

Pin and radio constants are defined per board and set automatically. Values below are for reference:

| Constant | V2 / TTGO V1 | V3 / V4 | Description |
|----------|--------------|---------|-------------|
| `LORA_CS` | 18 | 8 | SPI Chip Select |
| `LORA_RST` | 14 | 12 | Reset |
| `LORA_DIO0` | 26 | — | IRQ (SX1276 only) |
| `LORA_DIO1` | — | 14 | IRQ (SX1262 only) |
| `LORA_BUSY` | — | 13 | Busy (SX1262 only) |
| `LORA_FREQ` | 868.0 MHz | 868.0 MHz | Operating frequency |
| `LORA_TX_DB` | 17 dBm | 13 dBm (V3) / 22 dBm (V4) | Transmit power |
| `LORA_BAT_PIN` | 13 | 1 | ADC pin for battery voltage |
| `LORA_BAT_CTRL_PIN` | — | 37 | Pull LOW to enable battery ADC |
| `LORA_BAT_VREF` | 3.3 V | 3.3 V | ADC reference voltage |
| `LORA_BAT_DIV` | 2.0 | 2.0 | Voltage divider ratio |
