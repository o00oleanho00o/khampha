#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "secrets.h"

// =====================================================
// CONFIG
// =====================================================

static const char *OPENAI_URL =
    "https://api.openai.com/v1/responses";

static const char *OPENAI_MODEL =
    "gpt-5-mini";

// XIAO ESP32S3 onboard USER LED
static const int LED_PIN = 1;

// LED hiện tại đang bật hay tắt
bool ledState = false;

// Conversation hiện tại
String previousResponseId = "";


// =====================================================
// LED
// =====================================================

void setLed(bool state)
{
    ledState = state;

    // XIAO LED là active-low:
    // LOW  = ON
    // HIGH = OFF
    digitalWrite(
        LED_PIN,
        state ? LOW : HIGH);

    Serial.print("[Device] LED = ");
    Serial.println(state ? "ON" : "OFF");
}


// =====================================================
// WIFI
// =====================================================

bool connectWiFi()
{
    Serial.println();
    Serial.print("[WiFi] Connecting to ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long timeout = 20000;
    const unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");

        if (millis() - start > timeout)
        {
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


// =====================================================
// HTTP POST
// =====================================================

String postOpenAI(const String &body)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(
            "[WiFi] Lost connection. Reconnecting...");

        if (!connectWiFi())
        {
            return "";
        }
    }

    WiFiClientSecure client;

    // Prototype only
    client.setInsecure();

    HTTPClient http;

    if (!http.begin(client, OPENAI_URL))
    {
        Serial.println("[HTTP] begin() failed");
        return "";
    }

    http.setTimeout(90000);

    http.addHeader(
        "Content-Type",
        "application/json");

    http.addHeader(
        "Authorization",
        String("Bearer ") + OPENAI_API_KEY);

    Serial.println("[OpenAI] Sending...");

    int statusCode =
        http.POST(body);

    String response =
        http.getString();

    Serial.print("[HTTP] Status: ");
    Serial.println(statusCode);

    if (statusCode < 200 ||
        statusCode >= 300)
    {
        Serial.println(
            "[OpenAI] Error:");

        Serial.println(response);

        http.end();

        return "";
    }

    http.end();

    return response;
}


// =====================================================
// ADD TOOLS + COMMON SETTINGS
// =====================================================

void configureRequest(JsonDocument &request)
{
    request["model"] =
        OPENAI_MODEL;

    request["instructions"] =
        "You are Pet AI running on a XIAO ESP32S3. "
        "The user communicates through a serial terminal. "
        "Respond naturally in the same language as the user. "
        "Keep answers concise. "
        "You can physically control the onboard LED using "
        "the set_led tool. "
        "Whenever the user asks to turn the physical LED or "
        "light on or off, use set_led. "
        "Never claim that you changed the physical LED unless "
        "you actually use the set_led tool. "
        "Use web search only when current or up-to-date "
        "information is required.";

    request["max_output_tokens"] =
        1200;

    // Chỉ xử lý 1 custom tool một lần để firmware đơn giản
    request["parallel_tool_calls"] =
        false;

    request["tool_choice"] =
        "auto";


    // -------------------------------------------------
    // Reasoning
    // -------------------------------------------------

    JsonObject reasoning =
        request["reasoning"].to<JsonObject>();

    reasoning["effort"] =
        "low";


    // -------------------------------------------------
    // Output verbosity
    // -------------------------------------------------

    JsonObject text =
        request["text"].to<JsonObject>();

    text["verbosity"] =
        "low";


    // =================================================
    // TOOLS
    // =================================================

    JsonArray tools =
        request["tools"].to<JsonArray>();


    // -------------------------------------------------
    // TOOL 1: WEB SEARCH
    // -------------------------------------------------

    JsonObject webSearch =
        tools.add<JsonObject>();

    webSearch["type"] =
        "web_search";

    webSearch["search_context_size"] =
        "low";

    JsonObject location =
        webSearch["user_location"].to<JsonObject>();

    location["type"] =
        "approximate";

    location["country"] =
        "VN";

    location["timezone"] =
        "Asia/Ho_Chi_Minh";


    // -------------------------------------------------
    // TOOL 2: SET LED
    // -------------------------------------------------

    JsonObject ledTool =
        tools.add<JsonObject>();

    ledTool["type"] =
        "function";

    ledTool["name"] =
        "set_led";

    ledTool["description"] =
        "Turn the physical onboard LED of the "
        "XIAO ESP32S3 on or off.";

    ledTool["strict"] =
        true;


    // JSON Schema:
    //
    // {
    //   "state": true/false
    // }

    JsonObject parameters =
        ledTool["parameters"].to<JsonObject>();

    parameters["type"] =
        "object";


    JsonObject properties =
        parameters["properties"].to<JsonObject>();

    JsonObject stateProperty =
        properties["state"].to<JsonObject>();

    stateProperty["type"] =
        "boolean";

    stateProperty["description"] =
        "true means LED on, false means LED off";


    JsonArray required =
        parameters["required"].to<JsonArray>();

    required.add("state");

    parameters["additionalProperties"] =
        false;
}


// =====================================================
// EXTRACT ASSISTANT TEXT
// =====================================================

String extractOutputText(JsonDocument &doc)
{
    String result = "";

    JsonArray output =
        doc["output"].as<JsonArray>();

    for (JsonObject item : output)
    {
        const char *type =
            item["type"] | "";

        if (strcmp(type, "message") != 0)
        {
            continue;
        }

        JsonArray content =
            item["content"].as<JsonArray>();

        for (JsonObject part : content)
        {
            const char *partType =
                part["type"] | "";

            if (strcmp(
                    partType,
                    "output_text") != 0)
            {
                continue;
            }

            const char *text =
                part["text"] | "";

            if (strlen(text) == 0)
            {
                continue;
            }

            if (result.length() > 0)
            {
                result += "\n";
            }

            result += text;
        }
    }

    return result;
}


// =====================================================
// FIND FUNCTION CALL
// =====================================================

bool findFunctionCall(
    JsonDocument &doc,
    String &functionName,
    String &arguments,
    String &callId)
{
    JsonArray output =
        doc["output"].as<JsonArray>();

    for (JsonObject item : output)
    {
        const char *type =
            item["type"] | "";

        if (strcmp(
                type,
                "function_call") != 0)
        {
            continue;
        }

        functionName =
            item["name"].as<String>();

        arguments =
            item["arguments"].as<String>();

        callId =
            item["call_id"].as<String>();

        return true;
    }

    return false;
}


// =====================================================
// WEB SEARCH USED?
// =====================================================

bool responseUsedWebSearch(JsonDocument &doc)
{
    JsonArray output =
        doc["output"].as<JsonArray>();

    for (JsonObject item : output)
    {
        const char *type =
            item["type"] | "";

        if (strcmp(
                type,
                "web_search_call") == 0)
        {
            return true;
        }
    }

    return false;
}


// =====================================================
// TOKEN USAGE
// =====================================================

void printUsage(JsonDocument &doc)
{
    if (doc["usage"].isNull())
    {
        return;
    }

    int inputTokens =
        doc["usage"]["input_tokens"] | 0;

    int outputTokens =
        doc["usage"]["output_tokens"] | 0;

    int reasoningTokens =
        doc["usage"]["output_tokens_details"]
           ["reasoning_tokens"] | 0;

    int totalTokens =
        doc["usage"]["total_tokens"] | 0;

    Serial.print("[Usage] input=");
    Serial.print(inputTokens);

    Serial.print(" output=");
    Serial.print(outputTokens);

    Serial.print(" reasoning=");
    Serial.print(reasoningTokens);

    Serial.print(" total=");
    Serial.println(totalTokens);
}


// =====================================================
// EXECUTE LOCAL FUNCTION
// =====================================================

String executeFunction(
    const String &functionName,
    const String &arguments)
{
    Serial.println();

    Serial.print("[Tool] Function: ");
    Serial.println(functionName);

    Serial.print("[Tool] Arguments: ");
    Serial.println(arguments);


    // =================================================
    // set_led
    // =================================================

    if (functionName == "set_led")
    {
        JsonDocument argsDoc;

        DeserializationError error =
            deserializeJson(
                argsDoc,
                arguments);

        if (error)
        {
            Serial.println(
                "[Tool] Invalid JSON arguments");

            return
                "{\"success\":false,"
                "\"error\":\"invalid_arguments\"}";
        }

        bool state =
            argsDoc["state"] | false;

        // Đây là hành động vật lý thật
        setLed(state);


        // Kết quả trả lại cho AI
        JsonDocument resultDoc;

        resultDoc["success"] =
            true;

        resultDoc["led"] =
            state ? "on" : "off";

        String result;

        serializeJson(
            resultDoc,
            result);

        return result;
    }


    // Không nhận diện được function
    return
        "{\"success\":false,"
        "\"error\":\"unknown_function\"}";
}


// =====================================================
// BUILD TOOL RESULT REQUEST
// =====================================================

String sendFunctionResult(
    const String &responseId,
    const String &callId,
    const String &functionResult)
{
    JsonDocument request;

    configureRequest(request);

    // Tiếp tục ngay sau response chứa function_call
    request["previous_response_id"] =
        responseId;


    JsonArray input =
        request["input"].to<JsonArray>();

    JsonObject output =
        input.add<JsonObject>();

    output["type"] =
        "function_call_output";

    output["call_id"] =
        callId;

    output["output"] =
        functionResult;


    String body;

    serializeJson(
        request,
        body);

    return postOpenAI(body);
}


// =====================================================
// ASK AI
// =====================================================

void askAI(const String &userMessage)
{
    // =================================================
    // FIRST REQUEST
    // =================================================

    JsonDocument request;

    configureRequest(request);

    request["input"] =
        userMessage;

    if (!previousResponseId.isEmpty())
    {
        request["previous_response_id"] =
            previousResponseId;
    }


    String body;

    serializeJson(
        request,
        body);


    String response =
        postOpenAI(body);

    if (response.isEmpty())
    {
        Serial.println(
            "[AI] Empty response.");

        return;
    }


    // =================================================
    // TOOL LOOP
    //
    // Cho phép tối đa 3 vòng function calling.
    // =================================================

    for (int toolRound = 0;
         toolRound < 3;
         toolRound++)
    {
        JsonDocument responseDoc;

        DeserializationError error =
            deserializeJson(
                responseDoc,
                response);

        if (error)
        {
            Serial.print(
                "[JSON] Parse error: ");

            Serial.println(
                error.c_str());

            return;
        }


        String status =
            responseDoc["status"] | "unknown";


        // ---------------------------------------------
        // WEB SEARCH
        // ---------------------------------------------

        if (responseUsedWebSearch(
                responseDoc))
        {
            Serial.println(
                "[Tool] Web search used");
        }


        // ---------------------------------------------
        // USAGE
        // ---------------------------------------------

        printUsage(responseDoc);


        // ---------------------------------------------
        // INCOMPLETE
        // ---------------------------------------------

        if (status == "incomplete")
        {
            String reason =
                responseDoc
                    ["incomplete_details"]
                    ["reason"] |
                "unknown";

            Serial.print(
                "[AI] Response incomplete: ");

            Serial.println(reason);

            Serial.println(
                "[AI] Conversation state NOT advanced.");

            return;
        }


        // ---------------------------------------------
        // ERROR / OTHER STATUS
        // ---------------------------------------------

        if (status != "completed")
        {
            Serial.print(
                "[AI] Unexpected status: ");

            Serial.println(status);

            return;
        }


        String responseId =
            responseDoc["id"].as<String>();


        // =================================================
        // CHECK CUSTOM FUNCTION CALL
        // =================================================

        String functionName;
        String arguments;
        String callId;

        bool hasFunctionCall =
            findFunctionCall(
                responseDoc,
                functionName,
                arguments,
                callId);


        if (hasFunctionCall)
        {
            // -----------------------------------------
            // ESP32 EXECUTES REAL HARDWARE FUNCTION
            // -----------------------------------------

            String functionResult =
                executeFunction(
                    functionName,
                    arguments);


            Serial.print(
                "[Tool] Result: ");

            Serial.println(
                functionResult);


            // -----------------------------------------
            // SEND RESULT BACK TO MODEL
            // -----------------------------------------

            response =
                sendFunctionResult(
                    responseId,
                    callId,
                    functionResult);


            if (response.isEmpty())
            {
                Serial.println(
                    "[AI] Tool follow-up failed.");

                return;
            }

            // Có response mới.
            // Quay lại vòng for để parse tiếp.
            continue;
        }


        // =================================================
        // FINAL TEXT RESPONSE
        // =================================================

        String answer =
            extractOutputText(
                responseDoc);


        // Chỉ response cuối cùng mới trở thành
        // conversation head.
        previousResponseId =
            responseId;


        Serial.println();

        if (!answer.isEmpty())
        {
            Serial.print("AI> ");
            Serial.println(answer);
        }
        else
        {
            Serial.println(
                "[AI] Completed but no output_text.");
        }

        return;
    }


    Serial.println(
        "[AI] Too many tool calls.");
}


// =====================================================
// SERIAL
// =====================================================

void processSerial()
{
    if (!Serial.available())
    {
        return;
    }

    String input =
        Serial.readStringUntil('\n');

    input.trim();

    if (input.isEmpty())
    {
        return;
    }


    // =================================================
    // RESET CONVERSATION
    // =================================================

    if (input == "/reset")
    {
        previousResponseId = "";

        Serial.println();
        Serial.println(
            "[SYSTEM] Conversation reset.");

        Serial.println();
        Serial.print("You> ");

        return;
    }


    Serial.println();

    Serial.print("You> ");
    Serial.println(input);

    askAI(input);

    Serial.println();
    Serial.print("You> ");
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
    // =================================================
    // LED
    // =================================================

    pinMode(
        LED_PIN,
        OUTPUT);

    // LED active-low:
    // HIGH = OFF
    digitalWrite(
        LED_PIN,
        HIGH);

    ledState = false;


    // =================================================
    // SERIAL
    // =================================================

    Serial.begin(115200);

    delay(2500);

    Serial.println();
    Serial.println(
        "====================================");

    Serial.println(
        "        PET AI - XIAO V1");

    Serial.println(
        "        Chat + Web + LED");

    Serial.println(
        "====================================");


    // =================================================
    // WIFI
    // =================================================

    connectWiFi();


    Serial.println();

    Serial.println(
        "Ask me anything.");

    Serial.println(
        "AI can control the onboard LED.");

    Serial.println(
        "Web search is enabled.");

    Serial.println(
        "Type /reset to reset conversation.");

    Serial.println();

    Serial.print("You> ");
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
    processSerial();

    delay(10);
}