---
title: LoRaDomo Documentation
---

<style>
body { font-family: Arial, sans-serif; margin: 0; padding: 0; }
nav {
    position: fixed; top: 0; left: 0; width: 220px; height: 100%;
    background-color: #1E90FF; color: white; padding: 20px; box-sizing: border-box;
}
nav h2 { font-size: 18px; margin-top: 0; }
nav a { color: white; text-decoration: none; display: block; margin: 8px 0; }
nav a:hover { text-decoration: underline; }
main { margin-left: 240px; padding: 20px; }
h1 { color: #1E90FF; }
code { background-color: #f0f0f0; padding: 2px 4px; border-radius: 4px; }
pre { background-color: #f0f0f0; padding: 10px; border-radius: 4px; overflow-x: auto; }
</style>

<nav>
    <h2>LoRaDomo v1.0.0</h2>
    <a href="index.md">Home</a>
    <a href="installation.md">Installation</a>
    <a href="api.md">API Reference</a>
    <a href="architecture.md">Architecture</a>
    <a href="examples.md">Examples</a>
</nav>

<main>

# Welcome to LoRaDomo Documentation

LoRaDomo is an Arduino library for home automation using ESP32 and LoRa radios.  
It allows nodes to automatically register with a gateway, publish sensor values, and receive actuator commands.

---

## Quick Links

- [Installation](installation.md) — hardware & software setup, library installation, MQTT configuration
- [API Reference](api.md) — classes, functions, callbacks, constants
- [Architecture](architecture.md) — node/gateway design, MQTT topics, data flow
- [Examples](examples.md) — gateway and node example sketches

---

## Overview

LoRaDomo supports:

- Heltec WiFi LoRa 32 V3 boards (SX1262)  
- Automatic node registration and sensor publishing  
- Read and actuator callbacks  
- Periodic sending or on-change updates  
- Heartbeat with battery level  
- MQTT gateway with WebSocket UI  
- NVS persistence of node/sensor registry  
- Gateway reboot broadcast (nodes re-register automatically)  
- Debug mode (no code changes required)

---

## How It Works

1. **Node registration**: Nodes broadcast `MSG_NODE_PRESENT` → gateway acknowledges → node presents sensors → gateway acknowledges each sensor.
2. **Sensor publishing**: Registered nodes send sensor values periodically or on change.
3. **Actuator control**: Home automation controllers send commands via MQTT → gateway → LoRa → node.
4. **Heartbeat**: Nodes periodically send battery and uptime info to gateway and MQTT.
5. **Reboot handling**: Gateway reboot triggers all nodes to re-register automatically.

---

## Getting Started

1. Follow the [Installation](installation.md) guide to set up your hardware, software, and libraries.
2. Upload the Gateway example from [Examples](examples.md) to the gateway board.
3. Upload a Node example from [Examples](examples.md) to your node board(s).
4. Open the Web UI to monitor nodes and sensor values in real time.

---

## License

MIT License

</main>