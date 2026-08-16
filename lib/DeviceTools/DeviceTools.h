#pragma once

#include <Arduino.h>

namespace DeviceTools {
    void begin();
    String execute(const String &functionName, const String &argumentsJson);
}
