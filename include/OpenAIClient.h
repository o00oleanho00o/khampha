#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

class AudioManager;

class OpenAIClient {
public:
    // Existing Responses API transport used by the agent/tool loop.
    String postResponses(const String &jsonBody);

    // Uploads mono PCM16 as an in-memory WAV multipart/form-data request.
    String transcribePcm16(const int16_t *samples,
                           size_t sampleCount,
                           uint32_t sampleRate);

    // Streams OpenAI raw PCM TTS directly to AudioManager.
    bool speak(const String &text, AudioManager &audio);

private:
    struct HttpHeaders {
        int statusCode = -1;
        int64_t contentLength = -1;
        bool chunked = false;
        String contentType;
    };

    bool connectSecure(WiFiClientSecure &client);
    bool readHeaders(WiFiClientSecure &client, HttpHeaders &headers);
    String readTextBody(WiFiClientSecure &client, const HttpHeaders &headers);

    bool streamBinaryBodyToAudio(WiFiClientSecure &client,
                                 const HttpHeaders &headers,
                                 AudioManager &audio);

    bool readExact(WiFiClientSecure &client,
                   uint8_t *buffer,
                   size_t length,
                   uint32_t timeoutMs);

    String readLine(WiFiClientSecure &client, uint32_t timeoutMs);
};
