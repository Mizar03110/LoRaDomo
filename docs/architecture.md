# Architecture

## Overview

LoRaDomo implements a **star topology** LoRa network where a single gateway connects multiple nodes. The gateway bridges the LoRa network to WiFi/MQTT, making sensor data available to any home automation controller.

```
┌─────────────┐     LoRa      ┌──────────────────────────────────┐
│   Node 1    │ ←──────────→  │                                  │
│  (sensors)  │               │           Gateway                │   WiFi/MQTT
├─────────────┤               │        (Heltec V3)               │ ──────────→  MQTT Broker
│   Node 2    │ ←──────────→  │                                  │              Home Assistant
│  (sensors)  │               │  - LoRa receiver                 │              Node-RED...
├─────────────┤               │  - MQTT publisher                │
│   Node N    │ ←──────────→  │  - Web UI (port 80)              │
│  (sensors)  │               │  - WebSocket (port 81)           │
└─────────────┘               └──────────────────────────────────┘
```

---

## Class Hierarchy

```
LoRaNode  (base class)
    │
    └── LoRaGateway  (extends LoRaNode)
```

`LoRaNode` handles all LoRa communication, the registration state machine, sensor management, heartbeat, and actuator reception.

`LoRaGateway` extends `LoRaNode` with WiFi, MQTT, WebSocket UI, NVS persistence, and node/sensor registry management. It also supports **local sensors** (sensors attached directly to the gateway board) that publish to MQTT without any LoRa involvement — using the same `addSensor()` API as remote nodes.

---

## Node State Machine

Every node goes through three states before it can send sensor data:

```
  Power on
      │
      ▼
┌─────────────────┐   MSG_NODE_PRESENT (every 5s)
│  UNREGISTERED   │ ─────────────────────────────→  Gateway
│                 │ ←─────────────────────────────  MSG_ACK_NODE
└────────┬────────┘
         │ ACK received
         ▼
┌─────────────────┐   MSG_SENSOR_PRESENT (per sensor, every 5s)
│  REGISTERING    │ ─────────────────────────────→  Gateway
│                 │ ←─────────────────────────────  MSG_ACK_SENSOR
└────────┬────────┘
         │ All sensors acked
         ▼
┌─────────────────┐   MSG_SENSOR  (on interval or change)
│   REGISTERED    │ ─────────────────────────────→  Gateway
│                 │   MSG_HEARTBEAT (every 2 min)  →  Gateway
│                 │ ←─────────────────────────────  MSG_ACTUATOR
└─────────────────┘
```

**UNREGISTERED**: The node broadcasts `MSG_NODE_PRESENT` every 5 seconds until the gateway responds with `MSG_ACK_NODE`.

**REGISTERING**: The node presents each sensor one by one with `MSG_SENSOR_PRESENT`. The gateway responds with `MSG_ACK_SENSOR` for each. If a sensor does not receive an ACK within 5 seconds, it is retried. A failed sensor does not block others.

**REGISTERED**: Normal operation. The node sends sensor values automatically and listens for actuator commands. On first entering this state, it sends a heartbeat and all sensor values immediately.

---

## Registration Protocol

The inclusion mechanism ensures that:

- A **node cannot send sensor data** before it is registered
- A **sensor cannot receive commands** before its node is registered
- The network key (hashed) acts as **shared secret** — frames with a wrong key are silently discarded
- Registration is **non-blocking** — the node continues to loop() normally during the process

When the **gateway reboots**, it broadcasts `MSG_REQUEST_REFRESH`. Nodes that are already registered respond by resending all their current sensor values — **without resetting their state machine**. The gateway reloads its node/sensor registry from NVS before sending, so it already knows node names and sensor definitions. The same `MSG_REQUEST_REFRESH` is also sent each time a new WebSocket client connects, so late-joining browsers receive the current state immediately.

---

## Frame Structure

Every LoRa frame starts with a fixed header:

```
┌──────────┬──────────┬──────────────┬───────────┬───────────┬──────────┐
│ version  │  type    │   nodeID     │ messageID │   flags   │   key    │
│  1 byte  │  1 byte  │   4 bytes    │  1 byte   │  1 byte   │  2 bytes │
└──────────┴──────────┴──────────────┴───────────┴───────────┴──────────┘
```

- `version`: protocol version (currently 1)
- `type`: message type (see table below)
- `nodeID`: ESP32 chip ID (lower 32 bits of EFuse MAC), unique per device
- `messageID`: rolling counter, used for ACK matching
- `flags`: `FLAG_ACK_REQ` (0x01) when an ACK is expected
- `key`: FNV-1a 16-bit hash of the network key string

### Message Types

| Value | Name | Direction | Description |
|-------|------|-----------|-------------|
| 0x01 | MSG_NODE_PRESENT | Node → GW | Node announces itself |
| 0x02 | MSG_ACK_NODE | GW → Node | Gateway acknowledges node |
| 0x03 | MSG_SENSOR_PRESENT | Node → GW | Node announces a sensor |
| 0x04 | MSG_ACK_SENSOR | GW → Node | Gateway acknowledges sensor |
| 0x05 | MSG_SENSOR | Node → GW | Sensor value |
| 0x06 | MSG_HEARTBEAT | Node → GW | Battery + uptime |
| 0x07 | MSG_ACK | Internal | Generic ACK |
| 0x08 | MSG_REBOOT | GW → Node | Reboot command |
| 0x09 | MSG_ACTUATOR | GW → Node | Set sensor/actuator value |
| 0x0A | MSG_ACK_ACTUATOR | Node → GW | Acknowledge actuator |
| 0x0B | MSG_REQUEST_REFRESH | GW → All | Request all nodes to resend current sensor values |

---

## Heartbeat

Every node in `REGISTERED` state sends a `MSG_HEARTBEAT` every 2 minutes (configurable via `_heartbeatInterval`). The heartbeat carries:

- **nodeID**: the sender
- **battery**: 0–100% (set by user code via `_battery`)
- **uptime**: `millis()` in ms — time since last node boot

The gateway uses the uptime value to display the "online since" duration in the web UI. It also publishes the battery level to MQTT at `<gateway>/OUT/<node>/battery`.

If a node does not send any frame for **5 minutes**, the gateway marks it as offline and publishes its status as `0`.

---

## Sensor Auto-Send

Each sensor has an independent send interval (in seconds):

- `sendInterval > 0`: the node automatically sends the sensor value every N seconds. If a **read callback** is registered, it is called just before each send to fetch a fresh value from the hardware.
- `sendInterval == 0`: the value is only sent when it changes, i.e. when `sendInt8()`, `sendInt32()`, or `sendFloat()` is called explicitly.

On first entering `REGISTERED` state, **all sensors with a stored value** are sent immediately regardless of their interval.

---

## Actuator Flow

When the home automation controller wants to send a value to a node (e.g. turn on a relay):

```
Controller  →  MQTT Broker  →  Gateway  →  LoRa  →  Node
   publish         |           subscribe           receive
 IN/<node>/<s>     |           IN/#                MSG_ACTUATOR
                   |                               callback()
                   |                               sendInt8()  (confirm)
                   |           receive             MSG_SENSOR
                   |           publish
                   |         OUT/<node>/<s>
```

The gateway retries the actuator frame up to **3 times** (every 5 seconds) if the node does not respond with `MSG_ACK_ACTUATOR`. After 3 failed attempts, the message is silently dropped and logged (debug mode).

---

## Gateway Local Sensors

The gateway can host its own sensors (temperature probe, relay, etc.) using the same `addSensor()` / `setSensorXxx()` / `sendXxx()` API as remote nodes. These sensors are called **local sensors**.

```
Controller  →  MQTT Broker  →  Gateway (local sensor)
   publish         |           IN/<gw>/<s>
 IN/<gw>/<s>       |           actCallback() called directly
                   |
                   |           read callback → value
                   |           publish OUT/<gw>/<s>
```

Key differences from remote node sensors:
- Values are published directly to MQTT (`<gw>/OUT/<gw>/<sensor>`) — no LoRa frame involved
- Actuator commands arrive via MQTT (`<gw>/IN/<gw>/<sensor>`) and are handled locally
- Local sensors are shown in the web UI in a dedicated **gateway card** (with board name and uptime)
- On MQTT reconnect, all local sensor values are immediately republished

Internally, `LoRaGateway` overrides `transmitSensor()` (virtual in `LoRaNode`) to publish to MQTT instead of sending a LoRa frame.

---

## NVS Persistence

The gateway stores its **remote node** and sensor registry in the ESP32 **NVS** (Non-Volatile Storage) flash partition. This means node names and sensor definitions survive a gateway reboot without requiring the nodes to re-present themselves.

Stored per node: ID, name, board name, sensor count.
Stored per sensor: ID, data type, name.

Dynamic data (last sensor values, battery, online/offline durations) is **not persisted** — it is refreshed from the nodes after reboot via `MSG_REQUEST_REFRESH`.

NVS writes are **optimized**: they only occur when new nodes or sensors are discovered. Heartbeats and sensor value updates do not trigger NVS writes, preventing flash wear and FreeRTOS timing issues under load.

The **Delete all nodes** button in the web UI clears the entire registry from NVS immediately.

---

## Security

Security is based on a **shared network key**. The key string is hashed with FNV-1a into a 16-bit value stored in every frame header. Frames with a non-matching key are silently discarded by all receivers.

This provides basic network isolation (multiple LoRaDomo networks can coexist on the same frequency) but is **not cryptographic encryption**. Do not use this library for sensitive or safety-critical applications without adding application-level encryption.

---

## LoRa Radio Parameters

Default parameters (can be overridden with `setFrequency()`, `setTxPower()`, `setModemConfig()`):

| Parameter | Value |
|-----------|-------|
| Frequency | 868.0 MHz (Europe) |
| Bandwidth | 125 kHz |
| Coding rate | 4/5 |
| Spreading factor | 7 (SF128) |

TX power depends on the board: 17 dBm (V2, TTGO V1), 13 dBm (V3), 22 dBm (V4).

All nodes and the gateway must use the same frequency and modem config — they do by default since all use the same library.
