#include "Agent.h"

#include <cstring>

#include "AppConfig.h"
#include "DeviceTools.h"

Agent::Agent(OpenAIClient &client)
    : client_(client) {}

void Agent::resetConversation() {
    previousResponseId_ = "";
    Serial.println("[AI] Conversation reset");
}

void Agent::configureRequest(JsonDocument &request) {
    request["model"] = AppConfig::LLM_MODEL;

    request["instructions"] =
        "You are Pet AI running on a XIAO ESP32S3. "
        "The user usually speaks through a microphone and receives audio replies. "
        "Respond naturally in the same language as the user. "
        "Keep answers concise because the response will be spoken aloud. "
        "You can physically control the onboard LED using the set_led tool. "
        "Whenever the user asks to turn the physical LED or light on or off, use set_led. "
        "Never claim that you changed the physical LED unless you actually use set_led. "
        "Use web search only when current or up-to-date information is required.";

    request["max_output_tokens"] = 1200;
    request["parallel_tool_calls"] = false;
    request["tool_choice"] = "auto";

    JsonObject reasoning = request["reasoning"].to<JsonObject>();
    reasoning["effort"] = "low";

    JsonObject text = request["text"].to<JsonObject>();
    text["verbosity"] = "low";

    JsonArray tools = request["tools"].to<JsonArray>();

    JsonObject webSearch = tools.add<JsonObject>();
    webSearch["type"] = "web_search";
    webSearch["search_context_size"] = "low";

    JsonObject location = webSearch["user_location"].to<JsonObject>();
    location["type"] = "approximate";
    location["country"] = "VN";
    location["timezone"] = "Asia/Ho_Chi_Minh";

    JsonObject ledTool = tools.add<JsonObject>();
    ledTool["type"] = "function";
    ledTool["name"] = "set_led";
    ledTool["description"] =
        "Turn the physical onboard LED of the XIAO ESP32S3 on or off.";
    ledTool["strict"] = true;

    JsonObject parameters = ledTool["parameters"].to<JsonObject>();
    parameters["type"] = "object";

    JsonObject properties = parameters["properties"].to<JsonObject>();
    JsonObject stateProperty = properties["state"].to<JsonObject>();
    stateProperty["type"] = "boolean";
    stateProperty["description"] =
        "true means LED on, false means LED off";

    JsonArray required = parameters["required"].to<JsonArray>();
    required.add("state");
    parameters["additionalProperties"] = false;
}

String Agent::extractOutputText(JsonDocument &doc) {
    String result;
    JsonArray output = doc["output"].as<JsonArray>();

    for (JsonObject item : output) {
        const char *type = item["type"] | "";
        if (strcmp(type, "message") != 0) {
            continue;
        }

        JsonArray content = item["content"].as<JsonArray>();

        for (JsonObject part : content) {
            const char *partType = part["type"] | "";
            if (strcmp(partType, "output_text") != 0) {
                continue;
            }

            const char *text = part["text"] | "";
            if (strlen(text) == 0) {
                continue;
            }

            if (!result.isEmpty()) {
                result += "\n";
            }
            result += text;
        }
    }

    return result;
}

bool Agent::findFunctionCall(JsonDocument &doc,
                             String &functionName,
                             String &arguments,
                             String &callId) {
    JsonArray output = doc["output"].as<JsonArray>();

    for (JsonObject item : output) {
        const char *type = item["type"] | "";
        if (strcmp(type, "function_call") != 0) {
            continue;
        }

        functionName = item["name"].as<String>();
        arguments = item["arguments"].as<String>();
        callId = item["call_id"].as<String>();
        return true;
    }

    return false;
}

bool Agent::responseUsedWebSearch(JsonDocument &doc) {
    JsonArray output = doc["output"].as<JsonArray>();

    for (JsonObject item : output) {
        const char *type = item["type"] | "";
        if (strcmp(type, "web_search_call") == 0) {
            return true;
        }
    }

    return false;
}

void Agent::printUsage(JsonDocument &doc) {
    if (doc["usage"].isNull()) {
        return;
    }

    const int inputTokens = doc["usage"]["input_tokens"] | 0;
    const int outputTokens = doc["usage"]["output_tokens"] | 0;
    const int reasoningTokens =
        doc["usage"]["output_tokens_details"]["reasoning_tokens"] | 0;
    const int totalTokens = doc["usage"]["total_tokens"] | 0;

    Serial.print("[Usage] input=");
    Serial.print(inputTokens);
    Serial.print(" output=");
    Serial.print(outputTokens);
    Serial.print(" reasoning=");
    Serial.print(reasoningTokens);
    Serial.print(" total=");
    Serial.println(totalTokens);
}

String Agent::sendFunctionResult(const String &responseId,
                                 const String &callId,
                                 const String &functionResult) {
    JsonDocument request;
    configureRequest(request);

    request["previous_response_id"] = responseId;

    JsonArray input = request["input"].to<JsonArray>();
    JsonObject output = input.add<JsonObject>();
    output["type"] = "function_call_output";
    output["call_id"] = callId;
    output["output"] = functionResult;

    String body;
    serializeJson(request, body);
    return client_.postResponses(body);
}

String Agent::ask(const String &userMessage) {
    if (userMessage.isEmpty()) {
        return "";
    }

    JsonDocument request;
    configureRequest(request);
    request["input"] = userMessage;

    if (!previousResponseId_.isEmpty()) {
        request["previous_response_id"] = previousResponseId_;
    }

    String body;
    serializeJson(request, body);

    String response = client_.postResponses(body);
    if (response.isEmpty()) {
        Serial.println("[AI] Empty response");
        return "";
    }

    for (int toolRound = 0; toolRound < 3; ++toolRound) {
        JsonDocument responseDoc;
        const DeserializationError error =
            deserializeJson(responseDoc, response);

        if (error) {
            Serial.print("[AI] JSON parse error: ");
            Serial.println(error.c_str());
            return "";
        }

        const String status = responseDoc["status"] | "unknown";

        if (responseUsedWebSearch(responseDoc)) {
            Serial.println("[Tool] Web search used");
        }

        printUsage(responseDoc);

        if (status == "incomplete") {
            const String reason =
                responseDoc["incomplete_details"]["reason"] | "unknown";
            Serial.print("[AI] Response incomplete: ");
            Serial.println(reason);
            Serial.println("[AI] Conversation state NOT advanced");
            return "";
        }

        if (status != "completed") {
            Serial.print("[AI] Unexpected status: ");
            Serial.println(status);
            return "";
        }

        const String responseId = responseDoc["id"].as<String>();

        String functionName;
        String arguments;
        String callId;

        if (findFunctionCall(responseDoc,
                             functionName,
                             arguments,
                             callId)) {
            const String functionResult =
                DeviceTools::execute(functionName, arguments);

            Serial.print("[Tool] Result: ");
            Serial.println(functionResult);

            response = sendFunctionResult(responseId,
                                          callId,
                                          functionResult);

            if (response.isEmpty()) {
                Serial.println("[AI] Tool follow-up failed");
                return "";
            }

            continue;
        }

        const String answer = extractOutputText(responseDoc);

        // Only a completed final response becomes the conversation head.
        previousResponseId_ = responseId;

        if (!answer.isEmpty()) {
            Serial.print("AI> ");
            Serial.println(answer);
        } else {
            Serial.println("[AI] Completed but no output_text");
        }

        return answer;
    }

    Serial.println("[AI] Too many tool calls");
    return "";
}
