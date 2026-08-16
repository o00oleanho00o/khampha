#include "NetworkManager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "secrets.h"

bool NetworkManager::connectWiFi() {
    Serial.println();
    Serial.print("[WiFi] Connecting to ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");

        if (millis() - start > AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
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

bool NetworkManager::ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    Serial.println("[WiFi] Lost connection. Reconnecting...");
    return connectWiFi();
}
