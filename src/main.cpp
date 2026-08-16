#include <Arduino.h>

#include "AppConfig.h"
#include "NetworkManager.h"
#include "AudioManager.h"
#include "OpenAIClient.h"
#include "Agent.h"
#include "DeviceTools.h"
#include "VoicePipeline.h"

AudioManager audio;
OpenAIClient openAI;
Agent agent(openAI);
VoicePipeline voice(audio, openAI, agent);

static void printHelp() {
    Serial.println();
    Serial.println("Commands:");
    Serial.println("  /talk       Record 4 seconds -> STT -> AI -> TTS");
    Serial.println("  /talk N     Record N seconds (1..10)");
    Serial.println("  /reset      Reset AI conversation state");
    Serial.println("  /help       Show this help");
    Serial.println("  any text    Send text directly to AI, then speak reply");
    Serial.println();
}

static void processSerial() {
    if (!Serial.available()) {
        return;
    }

    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.isEmpty()) {
        return;
    }

    if (input == "/help") {
        printHelp();
        return;
    }

    if (input == "/reset") {
        agent.resetConversation();
        return;
    }

    if (input.startsWith("/talk")) {
        uint32_t seconds = AppConfig::RECORD_SECONDS;

        if (input.length() > 5) {
            String arg = input.substring(5);
            arg.trim();
            const int parsed = arg.toInt();
            if (parsed >= 1 && parsed <= static_cast<int>(AppConfig::MAX_RECORD_SECONDS)) {
                seconds = static_cast<uint32_t>(parsed);
            }
        }

        Serial.println();
        Serial.println("====================================");
        Serial.println("VOICE TURN");
        Serial.println("====================================");

        voice.runTurn(seconds);

        Serial.println();
        Serial.println("[Ready] Type /talk for another voice turn.");
        return;
    }

    // Keep serial text as a useful debug/fallback path.
    Serial.print("You> ");
    Serial.println(input);

    const String answer = agent.ask(input);
    if (!answer.isEmpty()) {
        openAI.speak(answer, audio);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2500);

    Serial.println();
    Serial.println("====================================");
    Serial.println("       PET AI - VOICE V1");
    Serial.println(" XIAO ESP32S3 + INMP441 + PCM5102A");
    Serial.println("====================================");

    DeviceTools::begin();

    if (!NetworkManager::connectWiFi()) {
        Serial.println("[SYSTEM] Wi-Fi failed. Device will retry on API calls.");
    }

    if (!audio.begin()) {
        Serial.println("[SYSTEM] Audio init failed. Stopping.");
        while (true) {
            delay(1000);
        }
    }

    Serial.println();
    Serial.println("Voice pipeline ready:");
    Serial.println("Mic -> STT -> GPT/tools -> TTS -> speaker");
    printHelp();
    Serial.println("[Ready] Type /talk, then speak when you see SPEAK NOW.");
}

void loop() {
    processSerial();
    delay(10);
}
