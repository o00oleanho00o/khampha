#include "DeviceTools.h"

#include <ArduinoJson.h>
#include "PetConfig.h"

namespace {
bool ledState = false;

void setLed(bool state) {
    ledState = state;
    digitalWrite(PetConfig::LED_PIN, state ? LOW : HIGH);
    Serial.printf("[Device] LED = %s\n", state ? "ON" : "OFF");
}
}  // namespace

namespace DeviceTools {

void begin() {
    pinMode(PetConfig::LED_PIN, OUTPUT);
    digitalWrite(PetConfig::LED_PIN, HIGH);
    ledState = false;
}

String execute(const String &functionName, const String &argumentsJson) {
    Serial.println();
    Serial.printf("[Tool] %s(%s)\n",
                  functionName.c_str(),
                  argumentsJson.c_str());

    if (functionName == "set_led") {
        JsonDocument args;
        const DeserializationError error =
            deserializeJson(args, argumentsJson);

        if (error) {
            return "{\"success\":false,\"error\":\"invalid_arguments\"}";
        }

        const bool state = args["state"] | false;
        setLed(state);

        JsonDocument result;
        result["success"] = true;
        result["led"] = state ? "on" : "off";

        String output;
        serializeJson(result, output);
        return output;
    }

    return "{\"success\":false,\"error\":\"unknown_function\"}";
}

}  // namespace DeviceTools
