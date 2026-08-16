#pragma once

#include <Arduino.h>
#include "AudioTools.h"
#include "AppConfig.h"

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    bool begin();

    // Records exactly 'seconds' seconds from the INMP441 and returns the
    // number of mono PCM16 samples stored in the internal PSRAM buffer.
    size_t recordFixed(uint32_t seconds);

    const int16_t *recordingData() const;
    size_t recordingSamples() const;
    uint32_t sampleRate() const;

    // TTS playback entry points. OpenAI sends mono PCM16 at 24 kHz.
    void beginPcmPlayback();
    bool playPcm16Bytes(const uint8_t *data, size_t byteCount);
    void endPcmPlayback();

private:
    AudioInfo audioInfo_;
    I2SStream i2s_;

    int16_t *recordBuffer_ = nullptr;
    size_t recordedSamples_ = 0;

    bool hasPendingPlaybackByte_ = false;
    uint8_t pendingPlaybackByte_ = 0;

    int32_t rxBuffer_[AppConfig::I2S_FRAMES_PER_BLOCK * AppConfig::AUDIO_CHANNELS];
    int32_t txBuffer_[AppConfig::I2S_FRAMES_PER_BLOCK * AppConfig::AUDIO_CHANNELS];

    int16_t convertMicSampleToPcm16(int32_t sample) const;
    bool writeStereoFrames(const int32_t *frames, size_t frameCount);
};
