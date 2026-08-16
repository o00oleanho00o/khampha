#include "OpenAIClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "secrets.h"

namespace {

struct __attribute__((packed)) WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t riffSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};

    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;
    uint16_t channels = 1;
    uint32_t sampleRate = 24000;
    uint32_t byteRate = 48000;
    uint16_t blockAlign = 2;
    uint16_t bitsPerSample = 16;

    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};

static_assert(sizeof(WavHeader) == 44, "WAV header must be 44 bytes");

}  // namespace

bool OpenAIClient::connectSecure(WiFiClientSecure &client) {
    if (!NetworkManager::ensureWiFi()) {
        return false;
    }

    // Prototype only. For a product, install and validate the CA certificate.
    client.setInsecure();
    client.setTimeout(AppConfig::HTTP_TIMEOUT_MS);

    if (!client.connect(AppConfig::OPENAI_HOST, AppConfig::OPENAI_PORT)) {
        Serial.println("[HTTPS] Connection to api.openai.com failed");
        return false;
    }

    return true;
}

String OpenAIClient::postResponses(const String &jsonBody) {
    if (!NetworkManager::ensureWiFi()) {
        return "";
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    const String url = String("https://") +
                       AppConfig::OPENAI_HOST +
                       AppConfig::RESPONSES_PATH;

    if (!http.begin(client, url)) {
        Serial.println("[HTTP] begin() failed");
        return "";
    }

    http.setTimeout(AppConfig::HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);

    Serial.println("[OpenAI] Sending Responses API request...");

    const int statusCode = http.POST(jsonBody);
    const String response = http.getString();

    Serial.print("[HTTP] Responses status: ");
    Serial.println(statusCode);

    if (statusCode < 200 || statusCode >= 300) {
        Serial.println("[OpenAI] Responses API error:");
        Serial.println(response);
        http.end();
        return "";
    }

    http.end();
    return response;
}

String OpenAIClient::readLine(WiFiClientSecure &client,
                              uint32_t timeoutMs) {
    String line;
    const uint32_t start = millis();

    while (millis() - start < timeoutMs) {
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (c == '\n') {
                return line;
            }
            if (c != '\r') {
                line += c;
            }
        }

        if (!client.connected() && !client.available()) {
            return line;
        }

        delay(1);
    }

    return line;
}

bool OpenAIClient::readExact(WiFiClientSecure &client,
                             uint8_t *buffer,
                             size_t length,
                             uint32_t timeoutMs) {
    size_t total = 0;
    uint32_t lastProgress = millis();

    while (total < length) {
        const int available = client.available();

        if (available > 0) {
            const size_t want = min(
                length - total,
                static_cast<size_t>(available));

            const int read = client.read(buffer + total, want);
            if (read > 0) {
                total += static_cast<size_t>(read);
                lastProgress = millis();
                continue;
            }
        }

        if (!client.connected() && !client.available()) {
            break;
        }

        if (millis() - lastProgress > timeoutMs) {
            break;
        }

        delay(1);
    }

    return total == length;
}

bool OpenAIClient::readHeaders(WiFiClientSecure &client,
                               HttpHeaders &headers) {
    const String statusLine = readLine(client, AppConfig::HTTP_TIMEOUT_MS);

    if (statusLine.length() == 0) {
        Serial.println("[HTTPS] Empty HTTP status line");
        return false;
    }

    int statusCode = -1;
    if (sscanf(statusLine.c_str(), "HTTP/%*s %d", &statusCode) != 1) {
        Serial.print("[HTTPS] Invalid status line: ");
        Serial.println(statusLine);
        return false;
    }

    headers.statusCode = statusCode;

    while (true) {
        String line = readLine(client, AppConfig::HTTP_TIMEOUT_MS);

        if (line.length() == 0) {
            break;
        }

        String lower = line;
        lower.toLowerCase();

        if (lower.startsWith("content-length:")) {
            const int colon = line.indexOf(':');
            if (colon >= 0) {
                String value = line.substring(colon + 1);
                value.trim();
                headers.contentLength = strtoll(value.c_str(), nullptr, 10);
            }
        } else if (lower.startsWith("transfer-encoding:")) {
            if (lower.indexOf("chunked") >= 0) {
                headers.chunked = true;
            }
        } else if (lower.startsWith("content-type:")) {
            const int colon = line.indexOf(':');
            if (colon >= 0) {
                headers.contentType = line.substring(colon + 1);
                headers.contentType.trim();
            }
        }
    }

    return true;
}

String OpenAIClient::readTextBody(WiFiClientSecure &client,
                                  const HttpHeaders &headers) {
    String body;
    uint8_t buffer[512];

    if (headers.chunked) {
        while (true) {
            String chunkLine = readLine(client, AppConfig::HTTP_TIMEOUT_MS);
            chunkLine.trim();

            const int semicolon = chunkLine.indexOf(';');
            if (semicolon >= 0) {
                chunkLine = chunkLine.substring(0, semicolon);
            }

            const size_t chunkSize =
                static_cast<size_t>(strtoul(chunkLine.c_str(), nullptr, 16));

            if (chunkSize == 0) {
                // Consume trailer headers, if any.
                while (true) {
                    const String trailer = readLine(client, AppConfig::HTTP_TIMEOUT_MS);
                    if (trailer.length() == 0) {
                        break;
                    }
                }
                break;
            }

            size_t remaining = chunkSize;
            while (remaining > 0) {
                const size_t n = min(remaining, sizeof(buffer));
                if (!readExact(client, buffer, n, AppConfig::HTTP_TIMEOUT_MS)) {
                    return body;
                }
                body.concat(reinterpret_cast<const char *>(buffer), n);
                remaining -= n;
            }

            // CRLF after each chunk.
            uint8_t crlf[2];
            if (!readExact(client, crlf, 2, AppConfig::HTTP_TIMEOUT_MS)) {
                return body;
            }
        }

        return body;
    }

    if (headers.contentLength >= 0) {
        int64_t remaining = headers.contentLength;

        while (remaining > 0) {
            const size_t n = static_cast<size_t>(
                remaining < static_cast<int64_t>(sizeof(buffer))
                    ? remaining
                    : static_cast<int64_t>(sizeof(buffer)));

            if (!readExact(client, buffer, n, AppConfig::HTTP_TIMEOUT_MS)) {
                break;
            }

            body.concat(reinterpret_cast<const char *>(buffer), n);
            remaining -= n;
        }

        return body;
    }

    uint32_t lastProgress = millis();

    while (client.connected() || client.available()) {
        const int available = client.available();

        if (available > 0) {
            const size_t n = min(
                static_cast<size_t>(available),
                sizeof(buffer));

            const int read = client.read(buffer, n);
            if (read > 0) {
                body.concat(reinterpret_cast<const char *>(buffer),
                            static_cast<size_t>(read));
                lastProgress = millis();
            }
        } else {
            if (millis() - lastProgress > AppConfig::HTTP_TIMEOUT_MS) {
                break;
            }
            delay(1);
        }
    }

    return body;
}

String OpenAIClient::transcribePcm16(const int16_t *samples,
                                     size_t sampleCount,
                                     uint32_t sampleRate) {
    if (samples == nullptr || sampleCount == 0) {
        return "";
    }

    WiFiClientSecure client;
    if (!connectSecure(client)) {
        return "";
    }

    WavHeader wav;
    wav.sampleRate = sampleRate;
    wav.byteRate = sampleRate * sizeof(int16_t);
    wav.dataSize = static_cast<uint32_t>(sampleCount * sizeof(int16_t));
    wav.riffSize = 36 + wav.dataSize;

    const String boundary = "----PetAIBoundary7MA4YWxk";

    const String modelPart =
        String("--") + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
        AppConfig::STT_MODEL + "\r\n";

    const String fileHeader =
        String("--") + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n" +
        "Content-Type: audio/wav\r\n\r\n";

    const String ending =
        String("\r\n--") + boundary + "--\r\n";

    const size_t contentLength =
        modelPart.length() +
        fileHeader.length() +
        sizeof(WavHeader) +
        wav.dataSize +
        ending.length();

    client.print(String("POST ") + AppConfig::TRANSCRIPTIONS_PATH + " HTTP/1.1\r\n");
    client.print(String("Host: ") + AppConfig::OPENAI_HOST + "\r\n");
    client.print(String("Authorization: Bearer ") + OPENAI_API_KEY + "\r\n");
    client.print(String("Content-Type: multipart/form-data; boundary=") + boundary + "\r\n");
    client.print(String("Content-Length: ") + contentLength + "\r\n");
    client.print("Connection: close\r\n\r\n");

    client.print(modelPart);
    client.print(fileHeader);
    client.write(reinterpret_cast<const uint8_t *>(&wav), sizeof(wav));

    const uint8_t *pcmBytes = reinterpret_cast<const uint8_t *>(samples);
    const size_t pcmByteCount = sampleCount * sizeof(int16_t);

    size_t sent = 0;
    while (sent < pcmByteCount) {
        const size_t chunk = min(static_cast<size_t>(4096), pcmByteCount - sent);
        const size_t written = client.write(pcmBytes + sent, chunk);

        if (written == 0) {
            Serial.println("[STT] Socket write failed");
            client.stop();
            return "";
        }

        sent += written;
    }

    client.print(ending);

    Serial.printf("[STT] Uploaded WAV: %u bytes PCM\n",
                  static_cast<unsigned>(pcmByteCount));

    HttpHeaders headers;
    if (!readHeaders(client, headers)) {
        client.stop();
        return "";
    }

    const String body = readTextBody(client, headers);
    client.stop();

    Serial.print("[HTTP] STT status: ");
    Serial.println(headers.statusCode);

    if (headers.statusCode < 200 || headers.statusCode >= 300) {
        Serial.println("[STT] API error:");
        Serial.println(body);
        return "";
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, body);

    if (error) {
        Serial.print("[STT] JSON parse error: ");
        Serial.println(error.c_str());
        Serial.println(body);
        return "";
    }

    String text = doc["text"] | "";
    text.trim();
    return text;
}

bool OpenAIClient::streamBinaryBodyToAudio(WiFiClientSecure &client,
                                           const HttpHeaders &headers,
                                           AudioManager &audio) {
    uint8_t buffer[1024];

    audio.beginPcmPlayback();

    if (headers.chunked) {
        while (true) {
            String chunkLine = readLine(client, AppConfig::HTTP_TIMEOUT_MS);
            chunkLine.trim();

            const int semicolon = chunkLine.indexOf(';');
            if (semicolon >= 0) {
                chunkLine = chunkLine.substring(0, semicolon);
            }

            const size_t chunkSize =
                static_cast<size_t>(strtoul(chunkLine.c_str(), nullptr, 16));

            if (chunkSize == 0) {
                while (true) {
                    const String trailer = readLine(client, AppConfig::HTTP_TIMEOUT_MS);
                    if (trailer.length() == 0) {
                        break;
                    }
                }
                audio.endPcmPlayback();
                return true;
            }

            size_t remaining = chunkSize;

            while (remaining > 0) {
                const size_t n = min(remaining, sizeof(buffer));

                if (!readExact(client, buffer, n, AppConfig::HTTP_TIMEOUT_MS)) {
                    audio.endPcmPlayback();
                    return false;
                }

                if (!audio.playPcm16Bytes(buffer, n)) {
                    audio.endPcmPlayback();
                    return false;
                }

                remaining -= n;
            }

            uint8_t crlf[2];
            if (!readExact(client, crlf, 2, AppConfig::HTTP_TIMEOUT_MS)) {
                audio.endPcmPlayback();
                return false;
            }
        }
    }

    if (headers.contentLength >= 0) {
        int64_t remaining = headers.contentLength;

        while (remaining > 0) {
            const size_t n = static_cast<size_t>(
                remaining < static_cast<int64_t>(sizeof(buffer))
                    ? remaining
                    : static_cast<int64_t>(sizeof(buffer)));

            if (!readExact(client, buffer, n, AppConfig::HTTP_TIMEOUT_MS)) {
                audio.endPcmPlayback();
                return false;
            }

            if (!audio.playPcm16Bytes(buffer, n)) {
                audio.endPcmPlayback();
                return false;
            }

            remaining -= n;
        }

        audio.endPcmPlayback();
        return true;
    }

    uint32_t lastProgress = millis();

    while (client.connected() || client.available()) {
        const int available = client.available();

        if (available > 0) {
            const size_t n = min(
                static_cast<size_t>(available),
                sizeof(buffer));

            const int read = client.read(buffer, n);
            if (read > 0) {
                if (!audio.playPcm16Bytes(buffer, static_cast<size_t>(read))) {
                    audio.endPcmPlayback();
                    return false;
                }
                lastProgress = millis();
            }
        } else {
            if (millis() - lastProgress > AppConfig::HTTP_TIMEOUT_MS) {
                audio.endPcmPlayback();
                return false;
            }
            delay(1);
        }
    }

    audio.endPcmPlayback();
    return true;
}

bool OpenAIClient::speak(const String &text, AudioManager &audio) {
    if (text.isEmpty()) {
        return false;
    }

    WiFiClientSecure client;
    if (!connectSecure(client)) {
        return false;
    }

    JsonDocument request;
    request["model"] = AppConfig::TTS_MODEL;
    request["voice"] = AppConfig::TTS_VOICE;
    request["input"] = text;
    request["response_format"] = "pcm";
    request["instructions"] =
        "Speak naturally and clearly. Match the language of the input text. "
        "For Vietnamese, use a warm natural Vietnamese delivery.";

    String body;
    serializeJson(request, body);

    client.print(String("POST ") + AppConfig::SPEECH_PATH + " HTTP/1.1\r\n");
    client.print(String("Host: ") + AppConfig::OPENAI_HOST + "\r\n");
    client.print(String("Authorization: Bearer ") + OPENAI_API_KEY + "\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print(String("Content-Length: ") + body.length() + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(body);

    HttpHeaders headers;
    if (!readHeaders(client, headers)) {
        client.stop();
        return false;
    }

    Serial.print("[HTTP] TTS status: ");
    Serial.println(headers.statusCode);

    if (headers.statusCode < 200 || headers.statusCode >= 300) {
        const String errorBody = readTextBody(client, headers);
        Serial.println("[TTS] API error:");
        Serial.println(errorBody);
        client.stop();
        return false;
    }

    const bool ok = streamBinaryBodyToAudio(client, headers, audio);
    client.stop();
    return ok;
}
