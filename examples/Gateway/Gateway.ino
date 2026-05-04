// ================= Gateway.ino — LoRaDomo V1.2 =================
// Example gateway sketch for Heltec V3

// Define your board BEFORE including the library:
//#define LORADOMO_HELTEC_V2
//#define LORADOMO_HELTEC_V3   // or LORADOMO_HELTEC_V4
//#define LORADOMO_TTGO_V1

#include "LoRaGateway.h"

LoRaGateway gateway;

// --- Gateway local sensor IDs ---
// These sensors live on the gateway itself: published to MQTT, shown in the web UI.
// No LoRa involved — the gateway reads them directly.
#define SENSOR_TEMP    1   // TYPE_FLOAT — sent every 30s, read via callback
#define SENSOR_HUM     2   // TYPE_INT8  — sent every 60s, read via callback
#define SENSOR_RELAY   3   // TYPE_INT8  — on-change only, controlled from MQTT

// --- Read callbacks: called just before each automatic publish ---
float  readTemperature() { return 21.5f + (random(-5, 6) / 10.0f); }
int8_t readHumidity()   { return 58 + random(-2, 3); }

// --- Actuator callback: called when the MQTT controller sends a value ---
void onRelayCommand(uint8_t sensorID, int8_t value) {
    Serial.printf("[App] Relay command: sensorID=%d value=%d\n", sensorID, value);
    // digitalWrite(RELAY_PIN, value ? HIGH : LOW);
    gateway.sendInt8(sensorID, value);   // confirm new state → published to MQTT
}

void setup() {
    Serial.begin(115200);

    gateway.begin(
        "MyGateway",        // gateway name (MQTT prefix + web UI title)
        "networkKey",       // shared network key (must match all nodes)
        "192.168.1.10",     // MQTT broker IP
        "mqttUser",         // MQTT username
        "mqttPass",         // MQTT password
        "MyWifi",           // WiFi SSID
        "wifiPass",         // WiFi password
        true                // debug mode
    );

    // addSensor(id, type, name, sendInterval_s, actCallback, readCallback)
    gateway.addSensor(SENSOR_TEMP,  TYPE_FLOAT, "temperature", 30,
                      nullptr, (void*)readTemperature);

    gateway.addSensor(SENSOR_HUM,   TYPE_INT8,  "humidity",    60,
                      nullptr, (void*)readHumidity);

    gateway.addSensor(SENSOR_RELAY, TYPE_INT8,  "relay",        0,
                      (void*)onRelayCommand, nullptr);

    // Pre-load initial values (published to MQTT as soon as WiFi/MQTT connects)
    gateway.setSensorFloat(SENSOR_TEMP,  21.5f);
    gateway.setSensorInt8 (SENSOR_HUM,   58);
    gateway.setSensorInt8 (SENSOR_RELAY, 0);
}

void loop() {
    gateway.loop();
}

// MQTT topics published by this gateway:
//   MyGateway/OUT/MyGateway/status           -> "1" online / "0" offline (LWT)
//   MyGateway/OUT/MyGateway/temperature      -> float value (every 30s)
//   MyGateway/OUT/MyGateway/humidity         -> int value  (every 60s)
//   MyGateway/OUT/MyGateway/relay            -> int value  (on change)
//   MyGateway/OUT/<nodeName>/status          -> "1" / "0"
//   MyGateway/OUT/<nodeName>/<sensorName>    -> numeric value
//   MyGateway/OUT/<nodeName>/battery         -> 0-100
//
// MQTT topics received by this gateway:
//   MyGateway/IN/MyGateway/relay             -> sets relay via onRelayCommand()
//   MyGateway/IN/<nodeName>/<sensorName>     -> forwarded to node via LoRa MSG_ACTUATOR
//
// Web UI  : http://<gateway_ip>/
// WebSocket: ws://<gateway_ip>:81
