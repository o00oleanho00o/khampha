#include "NetworkManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include "secrets.h"
#include "PetConfig.h"

namespace NetworkManager {

bool connectWiFi() {
    Serial.println();
    Serial.printf("[WiFi] Connecting to %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print('.');

        if (millis() - started > PetConfig::WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println();
            Serial.println("[WiFi] Connection timeout");
            return false;
        }
    }

    Serial.println();
    Serial.println("[WiFi] Connected");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
    return true;
}

bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    Serial.println("[WiFi] Connection lost. Reconnecting...");
    WiFi.disconnect();
    return connectWiFi();
}

}  // namespace NetworkManager
