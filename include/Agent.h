#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "OpenAIClient.h"

class Agent {
public:
    explicit Agent(OpenAIClient &client);

    // Returns final assistant text after any tool-call rounds.
    String ask(const String &userMessage);

    void resetConversation();

private:
    OpenAIClient &client_;
    String previousResponseId_;

    void configureRequest(JsonDocument &request);
    String extractOutputText(JsonDocument &doc);

    bool findFunctionCall(JsonDocument &doc,
                          String &functionName,
                          String &arguments,
                          String &callId);

    bool responseUsedWebSearch(JsonDocument &doc);
    void printUsage(JsonDocument &doc);

    String sendFunctionResult(const String &responseId,
                              const String &callId,
                              const String &functionResult);
};
