#include "RealtimeClient.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>

#include "DeviceTools.h"
#include "NetworkManager.h"
#include "secrets.h"

RealtimeClient::RealtimeClient(AudioEngine &audio)
    : audio_(audio) {}

RealtimeClient::~RealtimeClient() {
    if (audioRing_ != nullptr) {
        free(audioRing_);
        audioRing_ = nullptr;
    }
    if (decodedAudio_ != nullptr) {
        free(decodedAudio_);
        decodedAudio_ = nullptr;
    }
    if (websocket_) {
        websocket_->close();
    }
}

bool RealtimeClient::begin() {
    const size_t ringBytes =
        PetConfig::AUDIO_RING_SAMPLES * sizeof(int16_t);

    audioRing_ = static_cast<int16_t *>(
        heap_caps_malloc(ringBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (audioRing_ == nullptr) {
        audioRing_ = static_cast<int16_t *>(malloc(ringBytes));
    }

    decodedAudio_ = static_cast<uint8_t *>(
        heap_caps_malloc(PetConfig::MAX_AUDIO_DELTA_BYTES,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decodedAudio_ == nullptr) {
        decodedAudio_ = static_cast<uint8_t *>(
            malloc(PetConfig::MAX_AUDIO_DELTA_BYTES));
    }

    if (audioRing_ == nullptr || decodedAudio_ == nullptr) {
        Serial.println("[Realtime] ERROR: cannot allocate audio buffers");
        return false;
    }

    Serial.printf("[Realtime] Output ring: %u bytes (~%u s)\n",
                  static_cast<unsigned>(ringBytes),
                  static_cast<unsigned>(PetConfig::AUDIO_RING_SECONDS));

    assistantTranscript_.reserve(512);

    configureSocket();
    return true;
}

void RealtimeClient::configureSocket() {
    socketConnected_ = false;
    sessionReady_ = false;
    assistantTurnActive_ = false;
    finalResponseDone_ = false;
    sessionConfigStage_ = 0;
    sessionConfigSendPending_ = false;
    micPacketFill_ = 0;
    clearRing();

    socketUrl_ = String("wss://") + PetConfig::OPENAI_HOST +
                 "/v1/realtime?model=" + PetConfig::REALTIME_MODEL;

    // Recreate the client so reconnects cannot accumulate custom headers or
    // stale endpoint state.
    websocket_.reset();
    websocket_.reset(new websockets::WebsocketsClient());
    websocket_->setInsecure();  // Lab prototype only; add a CA for production.
    websocket_->addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
    websocket_->onMessage([this](websockets::WebsocketsMessage message) {
        const auto &data = message.rawData();
        handleServerMessage(
            reinterpret_cast<const uint8_t *>(data.data()), data.size());
    });
    websocket_->onEvent(
        [this](websockets::WebsocketsClient &client,
               websockets::WebsocketsEvent event,
               String detail) {
            if (event == websockets::WebsocketsEvent::GotPing) {
                client.pong(detail);
            }
            onWebsocketEvent(event, detail);
        });

    Serial.printf("[Realtime] Connecting: %s\n", socketUrl_.c_str());
    if (!websocket_->connect(socketUrl_)) {
        Serial.println("[Realtime] WebSocket connect failed");
        socketReconnectAtMs_ = millis() + PetConfig::WS_RECONNECT_MS;
    }
}

void RealtimeClient::onWebsocketEvent(websockets::WebsocketsEvent event,
                                      const String &detail) {
    switch (event) {
        case websockets::WebsocketsEvent::ConnectionOpened:
            socketConnected_ = true;
            sessionReady_ = false;
            sessionConfigStage_ = 0;
            sessionConfigSendPending_ = false;
            Serial.println("[Realtime] WebSocket connected");
            Serial.println("[Realtime] Waiting for session.created...");
            break;

        case websockets::WebsocketsEvent::ConnectionClosed:
            if (socketConnected_) {
                Serial.println("[Realtime] WebSocket disconnected");
                if (!detail.isEmpty()) {
                    Serial.printf("[Realtime] Disconnect detail: %s\n",
                                  detail.c_str());
                }
            }
            socketConnected_ = false;
            sessionReady_ = false;
            assistantTurnActive_ = false;
            finalResponseDone_ = false;
            sessionConfigStage_ = 0;
            sessionConfigSendPending_ = false;
            micPacketFill_ = 0;
            clearRing();
            socketReconnectAtMs_ = millis() + PetConfig::WS_RECONNECT_MS;
            break;

        default:
            break;
    }
}

bool RealtimeClient::sendSessionUpdateBase() {
    JsonDocument doc;
    doc["type"] = "session.update";

    JsonObject session = doc["session"].to<JsonObject>();
    session["type"] = "realtime";
    session["instructions"] =
        "You are Pet AI running on a XIAO ESP32S3 physical companion. "
        "Speak naturally in the same language as the user. "
        "Keep spoken replies concise, usually one or two short sentences. "
        "You can control the real onboard LED with the set_led tool. "
        "When the user asks to turn the physical LED/light on or off, use set_led. "
        "Never claim the LED changed unless the tool succeeded.";

    String message;
    serializeJson(doc, message);
    Serial.println("[Realtime] Config stage 1/3: base session");
    return sendJsonText(message);
}

bool RealtimeClient::sendSessionUpdateAudio() {
    JsonDocument doc;
    doc["type"] = "session.update";

    JsonObject session = doc["session"].to<JsonObject>();
    session["type"] = "realtime";

    JsonArray modalities = session["output_modalities"].to<JsonArray>();
    modalities.add("audio");

    JsonObject audio = session["audio"].to<JsonObject>();
    JsonObject input = audio["input"].to<JsonObject>();
    JsonObject inputFormat = input["format"].to<JsonObject>();
    inputFormat["type"] = "audio/pcm";
    inputFormat["rate"] = PetConfig::AUDIO_SAMPLE_RATE;

    JsonObject noiseReduction = input["noise_reduction"].to<JsonObject>();
    noiseReduction["type"] = "far_field";

    JsonObject vad = input["turn_detection"].to<JsonObject>();
    vad["type"] = "server_vad";
    vad["threshold"] = PetConfig::VAD_THRESHOLD;
    vad["prefix_padding_ms"] = PetConfig::VAD_PREFIX_PADDING_MS;
    vad["silence_duration_ms"] = PetConfig::VAD_SILENCE_DURATION_MS;
    vad["create_response"] = true;
    vad["interrupt_response"] = false;

    JsonObject output = audio["output"].to<JsonObject>();
    JsonObject outputFormat = output["format"].to<JsonObject>();
    outputFormat["type"] = "audio/pcm";
    outputFormat["rate"] = PetConfig::AUDIO_SAMPLE_RATE;
    output["voice"] = PetConfig::REALTIME_VOICE;

    String message;
    serializeJson(doc, message);
    Serial.printf("[Realtime] Config stage 2/3: audio 24kHz + VAD + voice=%s\n",
                  PetConfig::REALTIME_VOICE);
    return sendJsonText(message);
}

bool RealtimeClient::sendSessionUpdateTools() {
    JsonDocument doc;
    doc["type"] = "session.update";

    JsonObject session = doc["session"].to<JsonObject>();
    session["type"] = "realtime";
    session["max_output_tokens"] = PetConfig::MAX_OUTPUT_TOKENS;
    session["tool_choice"] = "auto";

    JsonArray tools = session["tools"].to<JsonArray>();
    JsonObject ledTool = tools.add<JsonObject>();
    ledTool["type"] = "function";
    ledTool["name"] = "set_led";
    ledTool["description"] = "Turn the physical onboard LED on or off.";

    JsonObject parameters = ledTool["parameters"].to<JsonObject>();
    parameters["type"] = "object";
    parameters["additionalProperties"] = false;
    JsonObject properties = parameters["properties"].to<JsonObject>();
    JsonObject state = properties["state"].to<JsonObject>();
    state["type"] = "boolean";
    state["description"] = "true = LED on, false = LED off";
    JsonArray required = parameters["required"].to<JsonArray>();
    required.add("state");

    String message;
    serializeJson(doc, message);
    Serial.println("[Realtime] Config stage 3/3: tools + limits");
    return sendJsonText(message);
}

void RealtimeClient::sendPendingSessionConfig() {
    if (!socketConnected_ || !sessionConfigSendPending_) {
        return;
    }

    // Keep configuration writes outside the receive callback so inbound frame
    // cleanup is complete before the next client event is transmitted.
    sessionConfigSendPending_ = false;

    bool sent = false;
    switch (sessionConfigStage_) {
        case 1:
            sent = sendSessionUpdateBase();
            break;
        case 2:
            sent = sendSessionUpdateAudio();
            break;
        case 3:
            sent = sendSessionUpdateTools();
            break;
        default:
            return;
    }

    if (!sent) {
        Serial.printf("[Realtime] ERROR: config stage %u send failed\n",
                      static_cast<unsigned>(sessionConfigStage_));
    }
}

bool RealtimeClient::sendJsonText(const String &json) {
    if (!socketConnected_) {
        return false;
    }

    // Print only small protocol configuration events. Never print the
    // continuous input_audio_buffer.append payloads.
    if (json.indexOf("\"type\":\"session.update\"") >= 0) {
        Serial.printf("[Realtime] TX session.update: %u bytes\n",
                      static_cast<unsigned>(json.length()));
        Serial.println(json);
    }

    return websocket_ && websocket_->send(json.c_str(), json.length());
}

bool RealtimeClient::sendResponseCreate() {
    return sendJsonText("{\"type\":\"response.create\"}");
}

bool RealtimeClient::sendFunctionOutput(const String &callId,
                                        const String &output) {
    JsonDocument doc;
    doc["type"] = "conversation.item.create";

    JsonObject item = doc["item"].to<JsonObject>();
    item["type"] = "function_call_output";
    item["call_id"] = callId;
    item["output"] = output;

    String message;
    serializeJson(doc, message);
    return sendJsonText(message);
}

bool RealtimeClient::jsonStringField(const uint8_t *payload,
                                     size_t length,
                                     const char *field,
                                     const char *&valueStart,
                                     size_t &valueLength) {
    if (payload == nullptr || field == nullptr) return false;

    String needle = String('"') + field + '"';
    const size_t needleLen = needle.length();

    for (size_t i = 0; i + needleLen < length; ++i) {
        if (memcmp(payload + i, needle.c_str(), needleLen) != 0) {
            continue;
        }

        size_t p = i + needleLen;
        while (p < length &&
               (payload[p] == ' ' || payload[p] == '\t' ||
                payload[p] == '\r' || payload[p] == '\n')) {
            ++p;
        }
        if (p >= length || payload[p] != ':') continue;
        ++p;

        while (p < length &&
               (payload[p] == ' ' || payload[p] == '\t' ||
                payload[p] == '\r' || payload[p] == '\n')) {
            ++p;
        }
        if (p >= length || payload[p] != '"') continue;
        ++p;

        const size_t start = p;
        bool escaped = false;
        while (p < length) {
            const char c = static_cast<char>(payload[p]);
            if (c == '"' && !escaped) {
                valueStart = reinterpret_cast<const char *>(payload + start);
                valueLength = p - start;
                return true;
            }
            if (c == '\\' && !escaped) {
                escaped = true;
            } else {
                escaped = false;
            }
            ++p;
        }
        return false;
    }

    return false;
}

bool RealtimeClient::jsonTypeEquals(const uint8_t *payload,
                                    size_t length,
                                    const char *expectedType) {
    const char *value = nullptr;
    size_t valueLen = 0;
    if (!jsonStringField(payload, length, "type", value, valueLen)) {
        return false;
    }

    const size_t expectedLen = strlen(expectedType);
    return valueLen == expectedLen &&
           memcmp(value, expectedType, expectedLen) == 0;
}

void RealtimeClient::handleServerMessage(const uint8_t *payload, size_t length) {
    // Audio deltas are frequent and large. Fast-path them without building
    // a large ArduinoJson tree for every packet.
    if (jsonTypeEquals(payload, length, "response.output_audio.delta")) {
        const char *delta = nullptr;
        size_t deltaLen = 0;
        if (jsonStringField(payload, length, "delta", delta, deltaLen)) {
            assistantTurnActive_ = true;
            enqueueAudioDelta(delta, deltaLen);
        }
        return;
    }

    JsonDocument doc;
    const DeserializationError error =
        deserializeJson(doc, payload, length);

    if (error) {
        Serial.printf("[Realtime] JSON error: %s (len=%u)\n",
                      error.c_str(),
                      static_cast<unsigned>(length));
        return;
    }

    const String type = doc["type"] | "";

    if (type == "session.created") {
        Serial.println("[Realtime] Session created");
        sessionConfigStage_ = 1;
        sessionConfigSendPending_ = true;
        return;
    }

    if (type == "session.updated") {
        if (sessionConfigStage_ == 1) {
            Serial.println("[Realtime] Config stage 1 accepted");
            sessionConfigStage_ = 2;
            sessionConfigSendPending_ = true;
            return;
        }

        if (sessionConfigStage_ == 2) {
            Serial.println("[Realtime] Config stage 2 accepted");
            sessionConfigStage_ = 3;
            sessionConfigSendPending_ = true;
            return;
        }

        if (sessionConfigStage_ == 3) {
            Serial.println("[Realtime] Config stage 3 accepted");
            sessionConfigStage_ = 4;
            sessionReady_ = true;
            micPacketFill_ = 0;
            Serial.println("[Realtime] Session ready. Speak normally.");
            return;
        }

        Serial.printf("[Realtime] Session updated (unexpected stage=%u)\n",
                      static_cast<unsigned>(sessionConfigStage_));
        return;
    }

    if (type == "input_audio_buffer.speech_started") {
        userSpeaking_ = true;
        Serial.println("[VAD] Speech started");
        return;
    }

    if (type == "input_audio_buffer.speech_stopped") {
        userSpeaking_ = false;
        Serial.println("[VAD] Speech stopped -> AI responding");
        return;
    }

    if (type == "response.created") {
        assistantTurnActive_ = true;
        finalResponseDone_ = false;
        assistantTranscript_ = "";
        micPacketFill_ = 0;
        Serial.println("[AI] Response started");
        return;
    }

    if (type == "response.output_audio_transcript.delta") {
        const char *delta = doc["delta"] | "";
        assistantTranscript_ += delta;
        return;
    }

    if (type == "response.output_audio_transcript.done") {
        const char *transcript = doc["transcript"] | "";
        if (strlen(transcript) > 0) {
            assistantTranscript_ = transcript;
        }
        if (!assistantTranscript_.isEmpty()) {
            Serial.print("AI> ");
            Serial.println(assistantTranscript_);
        }
        return;
    }

    if (type == "response.done") {
        handleResponseDone(doc);
        return;
    }

    if (type == "error") {
        const char *message = doc["error"]["message"] | "unknown realtime error";
        const char *code = doc["error"]["code"] | "";
        const char *errorType = doc["error"]["type"] | "";
        const char *param = doc["error"]["param"] | "";
        const char *eventId = doc["error"]["event_id"] | "";
        Serial.printf("[Realtime] API ERROR stage=%u type=%s code=%s param=%s event=%s: %s\n",
                      static_cast<unsigned>(sessionConfigStage_),
                      errorType,
                      code,
                      param,
                      eventId,
                      message);
        finalResponseDone_ = true;
        finishAssistantTurnIfPossible();
        return;
    }
}

void RealtimeClient::handleResponseDone(JsonDocument &doc) {
    const char *status = doc["response"]["status"] | "unknown";

    if (strcmp(status, "completed") != 0) {
        Serial.printf("[AI] Response finished with status=%s\n", status);
        finalResponseDone_ = true;
        finishAssistantTurnIfPossible();
        return;
    }

    bool hadFunctionCall = false;
    JsonArray output = doc["response"]["output"].as<JsonArray>();

    for (JsonObject item : output) {
        const char *itemType = item["type"] | "";
        if (strcmp(itemType, "function_call") != 0) {
            continue;
        }

        hadFunctionCall = true;

        const String functionName = item["name"].as<String>();
        const String arguments = item["arguments"].as<String>();
        const String callId = item["call_id"].as<String>();

        const String result = DeviceTools::execute(functionName, arguments);
        Serial.printf("[Tool] Result: %s\n", result.c_str());

        sendFunctionOutput(callId, result);
    }

    if (hadFunctionCall) {
        // Ask the realtime model to continue after local hardware execution.
        assistantTurnActive_ = true;
        finalResponseDone_ = false;
        sendResponseCreate();
        return;
    }

    finalResponseDone_ = true;
    finishAssistantTurnIfPossible();
}

void RealtimeClient::enqueueAudioDelta(const char *base64Data,
                                       size_t base64Length) {
    if (base64Data == nullptr || base64Length == 0) {
        return;
    }

    size_t decodedLength = 0;
    const int rc = mbedtls_base64_decode(
        decodedAudio_,
        PetConfig::MAX_AUDIO_DELTA_BYTES,
        &decodedLength,
        reinterpret_cast<const unsigned char *>(base64Data),
        base64Length);

    if (rc != 0) {
        Serial.printf("[Audio] Base64 decode failed rc=%d, b64=%u bytes\n",
                      rc,
                      static_cast<unsigned>(base64Length));
        return;
    }

    decodedLength &= ~static_cast<size_t>(1);  // whole PCM16 samples only
    if (decodedLength == 0) return;

    pushRingSamples(
        reinterpret_cast<const int16_t *>(decodedAudio_),
        decodedLength / sizeof(int16_t));
}

void RealtimeClient::pushRingSamples(const int16_t *samples, size_t count) {
    if (samples == nullptr || count == 0 || audioRing_ == nullptr) {
        return;
    }

    // If one packet is absurdly large, keep only its newest portion.
    if (count > PetConfig::AUDIO_RING_SAMPLES) {
        samples += count - PetConfig::AUDIO_RING_SAMPLES;
        count = PetConfig::AUDIO_RING_SAMPLES;
    }

    // Prefer low latency: drop oldest queued audio if the ring is full.
    const size_t freeSamples = PetConfig::AUDIO_RING_SAMPLES - ringCount_;
    if (count > freeSamples) {
        const size_t drop = count - freeSamples;
        ringTail_ = (ringTail_ + drop) % PetConfig::AUDIO_RING_SAMPLES;
        ringCount_ -= drop;
    }

    for (size_t i = 0; i < count; ++i) {
        audioRing_[ringHead_] = samples[i];
        ringHead_ = (ringHead_ + 1) % PetConfig::AUDIO_RING_SAMPLES;
    }
    ringCount_ += count;
}

size_t RealtimeClient::popRingSamples(int16_t *out, size_t maxCount) {
    if (out == nullptr || maxCount == 0 || ringCount_ == 0) {
        return 0;
    }

    const size_t count = min(maxCount, ringCount_);
    for (size_t i = 0; i < count; ++i) {
        out[i] = audioRing_[ringTail_];
        ringTail_ = (ringTail_ + 1) % PetConfig::AUDIO_RING_SAMPLES;
    }
    ringCount_ -= count;
    return count;
}

void RealtimeClient::clearRing() {
    ringHead_ = 0;
    ringTail_ = 0;
    ringCount_ = 0;
}

void RealtimeClient::servicePlayback() {
    if (ringCount_ == 0) {
        finishAssistantTurnIfPossible();
        return;
    }

    const size_t count = popRingSamples(
        playbackBlock_,
        PetConfig::PLAYBACK_BLOCK_SAMPLES);

    if (count > 0) {
        audio_.playPcm16(playbackBlock_, count);
    }
}

void RealtimeClient::finishAssistantTurnIfPossible() {
    if (!assistantTurnActive_ || !finalResponseDone_ || ringCount_ != 0) {
        return;
    }

    assistantTurnActive_ = false;
    finalResponseDone_ = false;
    micPacketFill_ = 0;
    micReopenAtMs_ = millis() + PetConfig::MIC_REOPEN_DELAY_MS;
    Serial.println("[AI] Playback done -> listening again");
}

bool RealtimeClient::sendMicPacket(const int16_t *samples,
                                   size_t sampleCount) {
    if (!socketConnected_ || !sessionReady_ ||
        samples == nullptr || sampleCount == 0) {
        return false;
    }

    const size_t pcmBytes = sampleCount * sizeof(int16_t);
    const size_t base64Capacity = 4 * ((pcmBytes + 2) / 3) + 1;

    // 40 ms packet = 1920 PCM bytes -> 2560 Base64 chars.
    char base64[4 * ((PetConfig::MIC_PACKET_SAMPLES * sizeof(int16_t) + 2) / 3) + 1];
    if (base64Capacity > sizeof(base64)) {
        return false;
    }

    size_t encodedLength = 0;
    const int rc = mbedtls_base64_encode(
        reinterpret_cast<unsigned char *>(base64),
        sizeof(base64),
        &encodedLength,
        reinterpret_cast<const unsigned char *>(samples),
        pcmBytes);

    if (rc != 0) {
        Serial.printf("[MIC] Base64 encode failed rc=%d\n", rc);
        return false;
    }

    base64[encodedLength] = '\0';

    String event;
    event.reserve(encodedLength + 64);
    event = "{\"type\":\"input_audio_buffer.append\",\"audio\":\"";
    event.concat(base64, encodedLength);
    event += "\"}";

    return websocket_ && websocket_->send(event.c_str(), event.length());
}

void RealtimeClient::captureAndSendMic() {
    const size_t readCount = audio_.readMicPcm16(
        micReadBlock_,
        PetConfig::I2S_FRAMES_PER_BLOCK);

    if (readCount == 0) {
        return;
    }

    size_t src = 0;
    while (src < readCount) {
        const size_t space = PetConfig::MIC_PACKET_SAMPLES - micPacketFill_;
        const size_t n = min(space, readCount - src);

        memcpy(micPacket_ + micPacketFill_,
               micReadBlock_ + src,
               n * sizeof(int16_t));

        micPacketFill_ += n;
        src += n;

        if (micPacketFill_ == PetConfig::MIC_PACKET_SAMPLES) {
            sendMicPacket(micPacket_, micPacketFill_);
            micPacketFill_ = 0;
        }
    }
}

void RealtimeClient::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        NetworkManager::ensureWiFi();
    }

    if (!socketConnected_) {
        if (static_cast<int32_t>(millis() - socketReconnectAtMs_) >= 0) {
            configureSocket();
        }
        delay(1);
        return;
    }

    websocket_->poll();

    // Process configuration writes only after the receive callback has fully
    // unwound inside websocket_->poll().
    sendPendingSessionConfig();

    if (!socketConnected_ || !sessionReady_) {
        delay(1);
        return;
    }

    // Audio playback gets priority. While speaking, mic audio is not sent
    // upstream; this prevents immediate self-echo before we implement AEC.
    if (ringCount_ > 0) {
        servicePlayback();
        return;
    }

    finishAssistantTurnIfPossible();

    if (assistantTurnActive_) {
        delay(1);
        return;
    }

    // Drain/discard a short acoustic tail and stale I2S RX data after the
    // model finishes speaking.
    if (static_cast<int32_t>(millis() - micReopenAtMs_) < 0) {
        audio_.readMicPcm16(micReadBlock_, PetConfig::I2S_FRAMES_PER_BLOCK);
        micPacketFill_ = 0;
        return;
    }

    if (micMuted_) {
        audio_.readMicPcm16(micReadBlock_, PetConfig::I2S_FRAMES_PER_BLOCK);
        micPacketFill_ = 0;
        return;
    }

    captureAndSendMic();
}

bool RealtimeClient::sendTextMessage(const String &text) {
    if (!sessionReady_ || text.isEmpty() || assistantTurnActive_) {
        return false;
    }

    JsonDocument doc;
    doc["type"] = "conversation.item.create";
    JsonObject item = doc["item"].to<JsonObject>();
    item["type"] = "message";
    item["role"] = "user";
    JsonArray content = item["content"].to<JsonArray>();
    JsonObject part = content.add<JsonObject>();
    part["type"] = "input_text";
    part["text"] = text;

    String message;
    serializeJson(doc, message);

    assistantTurnActive_ = true;
    finalResponseDone_ = false;
    micPacketFill_ = 0;

    if (!sendJsonText(message)) {
        assistantTurnActive_ = false;
        return false;
    }

    return sendResponseCreate();
}

void RealtimeClient::setMicMuted(bool muted) {
    micMuted_ = muted;
    micPacketFill_ = 0;
    Serial.printf("[MIC] %s\n", muted ? "MUTED" : "LISTENING");
}

bool RealtimeClient::isConnected() const {
    return socketConnected_;
}

bool RealtimeClient::isSessionReady() const {
    return sessionReady_;
}

bool RealtimeClient::isMicMuted() const {
    return micMuted_;
}

void RealtimeClient::reconnect() {
    Serial.println("[Realtime] Resetting conversation/session...");
    if (websocket_) {
        websocket_->close();
        websocket_.reset();
    }
    delay(200);
    configureSocket();
}

void RealtimeClient::printStatus() const {
    Serial.println();
    Serial.println("----- Realtime status -----");
    Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "down");
    Serial.printf("WebSocket: %s\n", socketConnected_ ? "connected" : "down");
    Serial.printf("Session: %s\n", sessionReady_ ? "ready" : "not ready");
    Serial.printf("Mic: %s\n", micMuted_ ? "muted" : "listening");
    Serial.printf("User speaking: %s\n", userSpeaking_ ? "yes" : "no");
    Serial.printf("Assistant turn: %s\n", assistantTurnActive_ ? "active" : "idle");
    Serial.printf("Queued audio: %u samples (%.2f s)\n",
                  static_cast<unsigned>(ringCount_),
                  static_cast<double>(ringCount_) / PetConfig::AUDIO_SAMPLE_RATE);
    Serial.printf("Model: %s\n", PetConfig::REALTIME_MODEL);
    Serial.println("---------------------------");
}
