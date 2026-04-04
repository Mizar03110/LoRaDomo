// ================= example_gateway.ino — LoRaDomo V1.0.0 =================
// Example gateway sketch for Heltec V3

#include "LoRaGateway.h"

LoRaGateway gateway;

void setup() {
    Serial.begin(115200);

    gateway.begin(
        "home",             // gateway name (MQTT prefix + web UI)
        "myLoRaNetwork",    // shared network key
        "192.168.1.10",     // MQTT broker
        "user",             // MQTT username
        "pass",             // MQTT password
        "MyWifi",           // WiFi SSID
        "WifiPassword",     // WiFi password
        true                // debug output (optional, default false)
    );
}

void loop() {
    gateway.loop();
}

// MQTT topics published:
//   home/OUT/home/status            -> "1" online / "0" offline (LWT)
//   home/OUT/<nodeName>/status      -> "online" / "offline"
//   home/OUT/<nodeName>/<sensorName> -> numeric value
//
// Web UI : http://<gateway_ip>/
// WebSocket : ws://<gateway_ip>:81
