# Installation

## Requirements

### Hardware

LoRaDomo currently supports the **Heltec WiFi LoRa 32 V3** board, which features:
- ESP32-S3 microcontroller
- SX1262 LoRa transceiver (pre-wired, no external wiring needed)
- Built-in OLED display (not used by this library)
- Built-in USB-C connector

You need **at least two** Heltec V3 boards: one acting as the **gateway**, the others as **nodes**.

> **Note:** Support for additional ESP32-based LoRa boards (TTGO LoRa32, Heltec V2 and V4, etc.) is planned for future versions. The library architecture is designed to make board porting straightforward — only the pin definitions and radio init in `LoRaNode.h` need to be adapted.

### Software

- [Arduino IDE 2.x](https://www.arduino.cc/en/software) or [PlatformIO](https://platformio.org/)
- ESP32 board support package (Arduino: install via Board Manager, search `esp32` by Espressif)

---

## Library Installation

### Option A — Arduino Library Manager (recommended)

1. Open Arduino IDE
2. Go to **Tools → Manage Libraries** (or **Sketch → Include Library → Manage Libraries**)
3. Search for `LoRaDomo`
4. Click **Install**
5. Done — dependencies are installed automatically if prompted

### Option B — Arduino IDE (ZIP)

1. Download `LoRaDomo_v1.0.0.zip`
2. Open Arduino IDE
3. Go to **Sketch → Include Library → Add .ZIP Library**
4. Select the downloaded ZIP
5. Done — the library appears under **Sketch → Include Library → LoRaDomo**

### Option C — PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/Mizar03110/LoRaDomo
```

Or by the the Arduino registry:

```ini
lib_deps =
    LoRaDomo
```

---

## Dependencies

The following libraries must be installed separately:

| Library | Install name | Purpose |
|---------|-------------|---------|
| RadioHead | `RadioHead` | SX1262 LoRa driver |
| PubSubClient | `PubSubClient` by Nick O'Leary | MQTT client |
| WebSockets | `WebSockets` by Markus Sattler | WebSocket server |
| ArduinoJson | `ArduinoJson` by Benoit Blanchon | JSON for WebSocket UI |

In Arduino IDE, install each via **Tools → Manage Libraries**.

In PlatformIO:

```ini
lib_deps =
    https://github.com/YOUR_USERNAME/LoRaDomo
    mikem/RadioHead
    knolleary/PubSubClient
    links2004/WebSockets
    bblanchon/ArduinoJson
```

---

## MQTT Broker

The gateway requires a running **MQTT broker** on your local network.
MQTT is a lightweight publish/subscribe messaging protocol widely used in home automation.

A popular choice is **Mosquitto**:

```bash
# Raspberry Pi / Debian
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable mosquitto
```

Minimal `/etc/mosquitto/mosquitto.conf`:

```
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Create a user:

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd myuser
```

The gateway connects to the broker and publishes sensor values on structured topics.
Your home automation controller (Home Assistant, Jeedom, Domoticz, Node-RED, etc.) subscribes to those topics to receive data.

---

## Verify Installation

Upload the **Gateway** example to one board and the **Node** example to another.
Open the Serial Monitor (115200 baud) with debug enabled.

You should see on the **node**:
```
[Node] begin() - radio OK freq=868.0MHz tx=13dBm
[Node] begin() - name='living_room' id=0x... key=0x...
[Node] addSensor() - id=1 type=2 name='temperature' interval=30s act=no read=no
[Node] sendNodePresent() - id=0x... name='living_room'
```

And on the **gateway**:
```
[GW] begin() - name='home' mqtt=192.168.1.10 ssid=MyWifi
[WiFi] Connecting...
[MQTT] Connecting... OK
[GW] handleNodePresent() - node accepted: id=0x... name='living_room' rssi=-24
[GW] sendAckNode() - ACK_NODE -> 0x...
```

Open a browser and navigate to the gateway's IP address to see the web dashboard.