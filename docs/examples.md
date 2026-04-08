# Examples

## Minimal Gateway

The simplest possible gateway — no debug output.

```cpp
#include "LoRaGateway.h"

LoRaGateway gateway;

void setup() {
    Serial.begin(115200);
    gateway.begin(
        "home",
        "myNetworkKey",
        "192.168.1.10",
        "mqttuser",
        "mqttpass",
        "MyWiFi",
        "wifipass"
    );
}

void loop() {
    gateway.loop();
}
```

The gateway will:
- Connect to WiFi and MQTT automatically (non-blocking, retries every 5s)
- Accept any node that presents itself with the correct key
- Publish sensor values to MQTT
- Serve the web UI at its IP address

---

## Minimal Node — Read-Only Sensors

A node with two sensors that send values periodically. No actuator, no read callback — values are pushed manually.

```cpp
#include "LoRaNode.h"

LoRaNode node;

#define SENSOR_TEMP 1
#define SENSOR_HUM  2

unsigned long lastRead = 0;

void setup() {
    Serial.begin(115200);
    node.begin("myNetworkKey", "bedroom");

    node.addSensor(SENSOR_TEMP, TYPE_FLOAT, "temperature", 60);
    node.addSensor(SENSOR_HUM,  TYPE_INT8,  "humidity",    60);

    // Pre-load initial values
    node.setSensorFloat(SENSOR_TEMP, 20.0f);
    node.setSensorInt8 (SENSOR_HUM,  50);
}

void loop() {
    node.loop();

    // Read hardware every 10s and push value
    // node.loop() will handle the actual LoRa send on its 60s schedule
    if (millis() - lastRead > 10000) {
        lastRead = millis();
        node.setSensorFloat(SENSOR_TEMP, readDHT22Temperature());
        node.setSensorInt8 (SENSOR_HUM,  readDHT22Humidity());
    }
}

float readDHT22Temperature() { /* your sensor code */ return 0; }
int8_t readDHT22Humidity()   { /* your sensor code */ return 0; }
```

---

## Node with Read Callbacks

The preferred pattern for periodic sensors: register a read callback so the library fetches the value automatically just before each LoRa transmission.

```cpp
#include "LoRaNode.h"
#include <DHT.h>

LoRaNode node;
DHT dht(4, DHT22);

#define SENSOR_TEMP 1
#define SENSOR_HUM  2

// Read callbacks — called automatically before each send
float readTemperature() {
    return dht.readTemperature();
}

int8_t readHumidity() {
    return (int8_t)dht.readHumidity();
}

void setup() {
    Serial.begin(115200);
    dht.begin();

    node.begin("myNetworkKey", "kitchen", true);  // debug enabled

    // Third argument: send interval (seconds)
    // Fifth argument: actuator callback (nullptr = read-only)
    // Sixth argument: read callback
    node.addSensor(SENSOR_TEMP, TYPE_FLOAT, "temperature", 30,
                   nullptr, (void*)readTemperature);

    node.addSensor(SENSOR_HUM,  TYPE_INT8,  "humidity",    60,
                   nullptr, (void*)readHumidity);

    // Pre-load for the initial burst (values before first hardware read)
    node.setSensorFloat(SENSOR_TEMP, dht.readTemperature());
    node.setSensorInt8 (SENSOR_HUM,  (int8_t)dht.readHumidity());
}

void loop() {
    node.loop();
    // Nothing else needed — read callbacks handle everything
}
```

MQTT output (every 30s for temperature, 60s for humidity):
```
home/OUT/kitchen/temperature  →  22.50
home/OUT/kitchen/humidity     →  58
```

---

## Node with Actuator (Relay)

A node that controls a relay. The relay state is sent on change only (`sendInterval = 0`), and a callback handles incoming commands from the home automation controller.

```cpp
#include "LoRaNode.h"

LoRaNode node;

#define SENSOR_RELAY 1
#define RELAY_PIN    5

// Called when gateway sends a new relay state
void onRelayCommand(uint8_t sensorID, int8_t value) {
    digitalWrite(RELAY_PIN, value ? HIGH : LOW);

    // Confirm the new state back to the gateway
    // This triggers a MSG_SENSOR back to the gateway,
    // which publishes the confirmed value to MQTT
    node.sendInt8(sensorID, value);
}

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);

    node.begin("myNetworkKey", "garage");

    // sendInterval=0: send on change only
    // actCallback: called when gateway sends a value
    node.addSensor(SENSOR_RELAY, TYPE_INT8, "light", 0,
                   (void*)onRelayCommand, nullptr);

    // Restore last state from EEPROM (example)
    int8_t savedState = 0; // EEPROM.read(0);
    digitalWrite(RELAY_PIN, savedState);
    node.setSensorInt8(SENSOR_RELAY, savedState);
}

void loop() {
    node.loop();
}
```

To turn on the relay, publish from your controller:
```
home/IN/garage/light  →  1
```

The gateway delivers the command to the node via LoRa. The node applies it, then confirms:
```
home/OUT/garage/light  →  1
```

---

## Node with Battery Reporting

On battery-powered nodes, register a battery callback so the library reads the ADC automatically before each heartbeat. The example below works on all supported boards using `#ifdef` for the control pin.

```cpp
#include "LoRaNode.h"

LoRaNode node;

// Called automatically before each heartbeat
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

float readSoilMoisture() {
    int raw = analogRead(34);
    return map(raw, 4095, 1500, 0, 100);  // calibrate to your sensor
}

void setup() {
    Serial.begin(115200);
    node.begin("myNetworkKey", "garden_sensor");
    node.setBatteryCallback(myBattery);
    node.addSensor(1, TYPE_FLOAT, "soil_moisture", 300,
                   nullptr, (void*)readSoilMoisture);
}

void loop() {
    node.loop();
}
```

The battery level and USB status are published on each heartbeat:
```
home/OUT/garden_sensor/battery  →  87
```

And displayed in the web UI next to the node name.

---

## Node with Multiple Sensor Types

A complete node mixing read-only sensors, an actuator, and battery reporting.

```cpp
#include "LoRaNode.h"
#include <DHT.h>
#include <BH1750.h>

LoRaNode node;
DHT    dht(4, DHT22);
BH1750 lightMeter;

#define SENSOR_TEMP  1
#define SENSOR_HUM   2
#define SENSOR_LUX   3
#define SENSOR_RELAY 4
#define RELAY_PIN    5

float   readTemp()  { return dht.readTemperature(); }
int8_t  readHum()   { return (int8_t)dht.readHumidity(); }
int32_t readLux()   { return (int32_t)lightMeter.readLightLevel(); }

void onRelay(uint8_t id, int8_t value) {
    digitalWrite(RELAY_PIN, value);
    node.sendInt8(id, value);
}

void setup() {
    Serial.begin(115200);
    dht.begin();
    lightMeter.begin();
    pinMode(RELAY_PIN, OUTPUT);

    node.begin("myNetworkKey", "living_room", true);

    node.addSensor(SENSOR_TEMP,  TYPE_FLOAT, "temperature",  30, nullptr,            (void*)readTemp);
    node.addSensor(SENSOR_HUM,   TYPE_INT8,  "humidity",     60, nullptr,            (void*)readHum);
    node.addSensor(SENSOR_LUX,   TYPE_INT32, "luminosity",   60, nullptr,            (void*)readLux);
    node.addSensor(SENSOR_RELAY, TYPE_INT8,  "relay",         0, (void*)onRelay,     nullptr);

    node.setSensorFloat(SENSOR_TEMP,  readTemp());
    node.setSensorInt8 (SENSOR_HUM,   readHum());
    node.setSensorInt32(SENSOR_LUX,   readLux());
    node.setSensorInt8 (SENSOR_RELAY, 0);
}

void loop() {
    node.loop();
    // battery is read automatically via setBatteryCallback()
}
```

---

## Home Assistant Integration

Once the gateway is running and publishing to MQTT, add sensors to Home Assistant via `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Living Room Temperature"
      state_topic: "home/OUT/living_room/temperature"
      unit_of_measurement: "°C"
      device_class: temperature

    - name: "Living Room Humidity"
      state_topic: "home/OUT/living_room/humidity"
      unit_of_measurement: "%"
      device_class: humidity

    - name: "Living Room Battery"
      state_topic: "home/OUT/living_room/battery"
      unit_of_measurement: "%"
      device_class: battery

  switch:
    - name: "Living Room Relay"
      state_topic: "home/OUT/living_room/relay"
      command_topic: "home/IN/living_room/relay"
      payload_on: "1"
      payload_off: "0"
      state_on: "1"
      state_off: "0"
```

---

## Deep Sleep (Battery Saving)

For very low power applications, a node can use ESP32 deep sleep between measurements. On wake, it re-registers from scratch.

```cpp
#include "LoRaNode.h"

LoRaNode node;

#define SLEEP_SECONDS 300  // wake every 5 minutes

void setup() {
    Serial.begin(115200);

    node.begin("myNetworkKey", "outdoor_sensor", true);
    node.addSensor(1, TYPE_FLOAT, "temperature", 0);  // interval=0, manual send

    // Pre-load value immediately
    node.setSensorFloat(1, readTemperature());
}

void loop() {
    node.loop();

    // Wait until REGISTERED, then send and sleep
    // The state machine handles registration automatically
    // Check via a flag set in a custom subclass, or use a timeout
}
```

> **Note**: Deep sleep resets the node state machine. The node always starts as `UNREGISTERED` on wake. The gateway handles re-registration gracefully since it keeps the node in its registry.
