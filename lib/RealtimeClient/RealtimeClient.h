#pragma once

#include <Arduino.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <memory>
#include "AudioEngine.h"
#include "PetConfig.h"

class RealtimeClient {
public:
    explicit RealtimeClient(AudioEngine &audio);
    ~RealtimeClient();

    bool begin();
    void loop();

    bool isConnected() const;
    bool isSessionReady() const;
    bool isMicMuted() const;

    void setMicMuted(bool muted);
    bool sendTextMessage(const String &text);
    void reconnect();
    void printStatus() const;

private:
    AudioEngine &audio_;
    std::unique_ptr<websockets::WebsocketsClient> websocket_;

    bool socketConnected_ = false;
    bool sessionReady_ = false;
    bool micMuted_ = false;
    bool userSpeaking_ = false;

    // Kept true from response.created through final local playback.
    bool assistantTurnActive_ = false;
    bool finalResponseDone_ = false;

    // Session configuration is applied in small stages so protocol errors
    // are easy to identify on embedded hardware.
    uint8_t sessionConfigStage_ = 0;
    bool sessionConfigSendPending_ = false;

    uint32_t micReopenAtMs_ = 0;
    uint32_t socketReconnectAtMs_ = 0;

    // Microphone streaming buffers.
    int16_t micReadBlock_[PetConfig::I2S_FRAMES_PER_BLOCK];
    int16_t micPacket_[PetConfig::MIC_PACKET_SAMPLES];
    size_t micPacketFill_ = 0;

    // PCM output queue in PSRAM.
    int16_t *audioRing_ = nullptr;
    size_t ringHead_ = 0;
    size_t ringTail_ = 0;
    size_t ringCount_ = 0;

    int16_t playbackBlock_[PetConfig::PLAYBACK_BLOCK_SAMPLES];
    uint8_t *decodedAudio_ = nullptr;

    // Transcript of model speech for Serial debugging.
    String assistantTranscript_;

    String socketUrl_;

    void configureSocket();
    void onWebsocketEvent(websockets::WebsocketsEvent event,
                          const String &detail);
    void handleServerMessage(const uint8_t *payload, size_t length);

    bool sendSessionUpdateBase();
    bool sendSessionUpdateAudio();
    bool sendSessionUpdateTools();
    void sendPendingSessionConfig();
    bool sendJsonText(const String &json);
    bool sendResponseCreate();
    bool sendFunctionOutput(const String &callId, const String &output);

    void captureAndSendMic();
    bool sendMicPacket(const int16_t *samples, size_t sampleCount);

    void enqueueAudioDelta(const char *base64Data, size_t base64Length);
    void pushRingSamples(const int16_t *samples, size_t count);
    size_t popRingSamples(int16_t *out, size_t maxCount);
    void clearRing();
    void servicePlayback();
    void finishAssistantTurnIfPossible();

    void handleResponseDone(JsonDocument &doc);

    static bool jsonStringField(const uint8_t *payload,
                                size_t length,
                                const char *field,
                                const char *&valueStart,
                                size_t &valueLength);
    static bool jsonTypeEquals(const uint8_t *payload,
                               size_t length,
                               const char *expectedType);
};
