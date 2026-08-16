#pragma once

#include <Arduino.h>

class DeviceTools {
public:
    static void begin();
    static void setLed(bool state);
    static bool ledState();

    // Dispatches an OpenAI custom function call and returns a JSON string
    // suitable for function_call_output.
    static String execute(const String &functionName, const String &arguments);

private:
    static bool ledState_;
};
