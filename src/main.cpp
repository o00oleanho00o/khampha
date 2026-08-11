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

// Giữ context hội thoại.
// Chỉ cập nhật khi response status = completed.
String previousResponseId = "";


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
// HTTP POST TO OPENAI
// =====================================================

String postOpenAI(const String &body)
{
    // Nếu WiFi bị mất thì reconnect
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Lost connection. Reconnecting...");

        if (!connectWiFi())
        {
            return "";
        }
    }

    WiFiClientSecure client;

    // =================================================
    // PROTOTYPE ONLY
    // Bỏ verify certificate để test cho đơn giản.
    // Sau này có thể chuyển sang CA certificate.
    // =================================================
    client.setInsecure();

    HTTPClient http;

    if (!http.begin(client, OPENAI_URL))
    {
        Serial.println("[HTTP] begin() failed");
        return "";
    }

    // Web search đôi khi mất vài giây
    http.setTimeout(90000);

    http.addHeader(
        "Content-Type",
        "application/json");

    http.addHeader(
        "Authorization",
        String("Bearer ") + OPENAI_API_KEY);

    Serial.println("[OpenAI] Sending...");

    int statusCode = http.POST(body);

    String response = http.getString();

    Serial.print("[HTTP] Status: ");
    Serial.println(statusCode);

    if (statusCode < 200 || statusCode >= 300)
    {
        Serial.println("[OpenAI] Error:");
        Serial.println(response);

        http.end();
        return "";
    }

    http.end();

    return response;
}


// =====================================================
// EXTRACT OUTPUT TEXT
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

        // Chỉ lấy assistant message
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

            if (strcmp(partType, "output_text") != 0)
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
// CHECK IF WEB SEARCH WAS USED
// =====================================================

bool responseUsedWebSearch(JsonDocument &doc)
{
    JsonArray output =
        doc["output"].as<JsonArray>();

    for (JsonObject item : output)
    {
        const char *type =
            item["type"] | "";

        if (strcmp(type, "web_search_call") == 0)
        {
            return true;
        }
    }

    return false;
}


// =====================================================
// PRINT TOKEN USAGE
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
        doc["usage"]["output_tokens_details"]["reasoning_tokens"] | 0;

    int totalTokens =
        doc["usage"]["total_tokens"] | 0;

    Serial.println();
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
// ASK OPENAI
// =====================================================

void askAI(const String &userMessage)
{
    // -------------------------------------------------
    // Build request
    // -------------------------------------------------

    JsonDocument request;

    request["model"] = OPENAI_MODEL;

    request["instructions"] =
        "You are Pet AI running on a XIAO ESP32S3. "
        "The user is communicating through a serial terminal. "
        "Respond naturally in the same language as the user. "
        "Keep answers concise. "
        "Use web search only when the question requires current "
        "or up-to-date information. "
        "If a question depends on the user's exact location and "
        "the user did not provide a location, ask for it.";

    request["input"] =
        userMessage;

    // Quan trọng:
    // 500 trước đây quá thấp khi GPT-5 dùng reasoning + web search.
    request["max_output_tokens"] = 1200;


    // -------------------------------------------------
    // Reasoning
    //
    // GPT-5-mini mặc định có thể dành khá nhiều token
    // cho reasoning. Dùng low cho prototype nhanh/rẻ hơn.
    // -------------------------------------------------

    JsonObject reasoning =
        request["reasoning"].to<JsonObject>();

    reasoning["effort"] =
        "low";


    // -------------------------------------------------
    // Text output
    // -------------------------------------------------

    JsonObject text =
        request["text"].to<JsonObject>();

    text["verbosity"] =
        "low";


    // -------------------------------------------------
    // WEB SEARCH TOOL
    // -------------------------------------------------

    JsonArray tools =
        request["tools"].to<JsonArray>();

    JsonObject webSearch =
        tools.add<JsonObject>();

    webSearch["type"] =
        "web_search";

    // Dùng ít search context hơn:
    // nhanh hơn + ít token hơn cho prototype
    webSearch["search_context_size"] =
        "low";


    // -------------------------------------------------
    // Approximate location cho Web Search
    //
    // Không hard-code thành phố.
    // Nếu hỏi "thời tiết Hà Nội" thì model dùng Hà Nội.
    //
    // Nếu thiết bị của bạn không dùng ở Việt Nam,
    // hãy đổi hai giá trị này.
    // -------------------------------------------------

    JsonObject location =
        webSearch["user_location"].to<JsonObject>();

    location["type"] =
        "approximate";

    location["country"] =
        "VN";

    location["timezone"] =
        "Asia/Ho_Chi_Minh";


    // -------------------------------------------------
    // Conversation memory
    // -------------------------------------------------

    if (!previousResponseId.isEmpty())
    {
        request["previous_response_id"] =
            previousResponseId;
    }


    // -------------------------------------------------
    // Serialize JSON
    // -------------------------------------------------

    String body;

    serializeJson(
        request,
        body);


    // -------------------------------------------------
    // Send request
    // -------------------------------------------------

    String response =
        postOpenAI(body);

    if (response.isEmpty())
    {
        Serial.println("[AI] Empty response.");
        return;
    }


    // -------------------------------------------------
    // Parse response
    // -------------------------------------------------

    JsonDocument responseDoc;

    DeserializationError error =
        deserializeJson(
            responseDoc,
            response);

    if (error)
    {
        Serial.print("[JSON] Parse error: ");
        Serial.println(error.c_str());

        Serial.print("[JSON] Response length: ");
        Serial.println(response.length());

        return;
    }


    // -------------------------------------------------
    // Read status
    // -------------------------------------------------

    String status =
        responseDoc["status"] | "unknown";


    // -------------------------------------------------
    // Show whether web search happened
    // -------------------------------------------------

    if (responseUsedWebSearch(responseDoc))
    {
        Serial.println("[Tool] Web search used");
    }


    // -------------------------------------------------
    // Token usage
    // -------------------------------------------------

    printUsage(responseDoc);


    // -------------------------------------------------
    // Extract assistant text
    // -------------------------------------------------

    String answer =
        extractOutputText(responseDoc);


    // =================================================
    // COMPLETED
    // =================================================

    if (status == "completed")
    {
        // Chỉ completed mới được trở thành conversation head.
        if (!responseDoc["id"].isNull())
        {
            previousResponseId =
                responseDoc["id"].as<String>();
        }

        Serial.println();

        if (!answer.isEmpty())
        {
            Serial.print("AI> ");
            Serial.println(answer);
        }
        else
        {
            Serial.println(
                "[AI] Response completed but no output_text was found.");
        }

        return;
    }


    // =================================================
    // INCOMPLETE
    // =================================================

    if (status == "incomplete")
    {
        String reason =
            responseDoc["incomplete_details"]["reason"] | "unknown";

        Serial.println();
        Serial.print("[AI] Response incomplete: ");
        Serial.println(reason);

        // Nếu có partial text thì vẫn cho xem,
        // nhưng KHÔNG nối conversation vào response này.
        if (!answer.isEmpty())
        {
            Serial.println();
            Serial.print("AI (partial)> ");
            Serial.println(answer);
        }

        Serial.println(
            "[AI] Conversation state NOT advanced.");

        return;
    }


    // =================================================
    // OTHER STATUS
    // =================================================

    Serial.println();

    Serial.print("[AI] Unexpected response status: ");
    Serial.println(status);

    // Không cập nhật previousResponseId
}


// =====================================================
// SERIAL INPUT
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


    // -------------------------------------------------
    // Local reset command
    // -------------------------------------------------

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


    // -------------------------------------------------
    // Send to AI
    // -------------------------------------------------

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
    Serial.begin(115200);

    // Cho USB CDC có thời gian enumerate
    delay(2500);

    Serial.println();
    Serial.println(
        "====================================");

    Serial.println(
        "       PET AI - SERIAL V0");

    Serial.println(
        "       XIAO ESP32S3");

    Serial.println(
        "====================================");

    connectWiFi();

    Serial.println();

    Serial.println(
        "Ask me anything.");

    Serial.println(
        "Web search is enabled.");

    Serial.println(
        "Type /reset to start a new conversation.");

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