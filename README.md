# LoRaDomo v1.0.0

LoRa home automation library

## 📚 Full Documentation

[Full Documentation](https://mizar03110.github.io/LoRaDomo/)

## Features

- Automatic node registration with gateway
- Sensor value publishing (int8, int32, float)
- Read callbacks: fetch fresh sensor value just before send
- Actuator callbacks: receive values from gateway/domotics controller
- Periodic send interval per sensor (or on-change only)
- Heartbeat with battery level
- MQTT gateway with WebSocket UI
- NVS persistence of node/sensor registry
- Gateway reboot broadcast (nodes re-register automatically)
- Debug mode (no code change needed)

## Dependencies

Install via Arduino Library Manager or PlatformIO:

- RadioHead
- PubSubClient
- WebSockets
- ArduinoJson

## Hardware

![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![LoRa](https://img.shields.io/badge/LoRa-SX1262-green)

- Heltec WiFi LoRa 32 V3 (SX1262)
- Pins: CS=8, DIO1=14, BUSY=13, RST=12
- Frequency: 868 MHz (Europe)
- Modem: BW125, CR4/5, SF128

## Quick Start

### Gateway

```cpp
#include "LoRaGateway.h"

LoRaGateway gateway;

void setup() {
    Serial.begin(115200);
    gateway.begin(
        "home",
        "networkKey",
        "192.168.1.10",
        "user", "pass",
        "MyWifi", "pass",
        true
    );
}

void loop() { gateway.loop(); }


