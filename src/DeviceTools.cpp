#include "DeviceTools.h"

#include <ArduinoJson.h>

#include "AppConfig.h"

bool DeviceTools::ledState_ = false;

void DeviceTools::begin() {
    pinMode(AppConfig::LED_PIN, OUTPUT);

    // XIAO onboard LED in the user's current firmware is active-low.
    digitalWrite(AppConfig::LED_PIN, HIGH);
    ledState_ = false;
}

void DeviceTools::setLed(bool state) {
    ledState_ = state;
    digitalWrite(AppConfig::LED_PIN, state ? LOW : HIGH);

    Serial.print("[Device] LED = ");
    Serial.println(state ? "ON" : "OFF");
}

bool DeviceTools::ledState() {
    return ledState_;
}

String DeviceTools::execute(const String &functionName,
                            const String &arguments) {
    Serial.println();
    Serial.print("[Tool] Function: ");
    Serial.println(functionName);
    Serial.print("[Tool] Arguments: ");
    Serial.println(arguments);

    if (functionName == "set_led") {
        JsonDocument argsDoc;
        const DeserializationError error = deserializeJson(argsDoc, arguments);

        if (error) {
            Serial.println("[Tool] Invalid JSON arguments");
            return "{\"success\":false,\"error\":\"invalid_arguments\"}";
        }

        const bool state = argsDoc["state"] | false;
        setLed(state);

        JsonDocument resultDoc;
        resultDoc["success"] = true;
        resultDoc["led"] = state ? "on" : "off";

        String result;
        serializeJson(resultDoc, result);
        return result;
    }

    return "{\"success\":false,\"error\":\"unknown_function\"}";
}
