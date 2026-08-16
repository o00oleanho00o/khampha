#include <Arduino.h>
#include <WiFi.h>  // Force PlatformIO to resolve the ESP32 framework WiFi library before WebSockets.

#include "AudioEngine.h"
#include "DeviceTools.h"
#include "NetworkManager.h"
#include "PetConfig.h"
#include "RealtimeClient.h"

AudioEngine audio;
RealtimeClient realtime(audio);

static void printHelp() {
    Serial.println();
    Serial.println("Commands:");
    Serial.println("  /status       Show Realtime state");
    Serial.println("  /mute         Stop sending mic audio");
    Serial.println("  /unmute       Resume mic audio");
    Serial.println("  /reset        New Realtime session / clear conversation");
    Serial.println("  /help         Show commands");
    Serial.println("  any text      Send a text turn through the same Realtime session");
    Serial.println();
    Serial.println("Normal use: do not type /talk. Just speak when session is ready.");
}

static void processSerial() {
    if (!Serial.available()) {
        return;
    }

    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.isEmpty()) return;

    if (input == "/help") {
        printHelp();
        return;
    }

    if (input == "/status") {
        realtime.printStatus();
        return;
    }

    if (input == "/mute") {
        realtime.setMicMuted(true);
        return;
    }

    if (input == "/unmute") {
        realtime.setMicMuted(false);
        return;
    }

    if (input == "/reset") {
        realtime.reconnect();
        return;
    }

    Serial.print("You(text)> ");
    Serial.println(input);

    if (!realtime.sendTextMessage(input)) {
        Serial.println("[Serial] Cannot send text now (session not ready or AI is speaking).");
    }
}

void setup() {
    Serial.begin(115200);
    delay(2500);

    Serial.println();
    Serial.println("============================================");
    Serial.println("       PET AI - REALTIME VOICE V1.5");
    Serial.println(" XIAO ESP32S3 + INMP441 + PCM5102A");
    Serial.println("============================================");
    Serial.printf("Model: %s\n", PetConfig::REALTIME_MODEL);
    Serial.println("Build: realtime-v1.3-sendtxt-fix\nPipeline: Mic PCM -> WebSocket Realtime -> PCM speaker");

    DeviceTools::begin();

    if (!NetworkManager::connectWiFi()) {
        Serial.println("[SYSTEM] Initial WiFi failed; loop will retry.");
    }

    if (!audio.begin()) {
        Serial.println("[SYSTEM] Audio init failed. Stopping.");
        while (true) delay(1000);
    }

    if (!realtime.begin()) {
        Serial.println("[SYSTEM] Realtime init failed. Stopping.");
        while (true) delay(1000);
    }

    printHelp();
    Serial.println("[SYSTEM] Waiting for Realtime session...");
}

void loop() {
    realtime.loop();
    processSerial();
}
