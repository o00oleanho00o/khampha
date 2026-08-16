#include "AudioEngine.h"

#include <limits.h>
#include <esp_heap_caps.h>

AudioEngine::AudioEngine()
    : audioInfo_(PetConfig::AUDIO_SAMPLE_RATE,
                 PetConfig::I2S_CHANNELS,
                 PetConfig::I2S_BITS_PER_SAMPLE) {}

bool AudioEngine::begin() {
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

    Serial.printf("[Audio] PSRAM: %s\n", psramFound() ? "yes" : "no");

    auto config = i2s_.defaultConfig(RXTX_MODE);
    config.copyFrom(audioInfo_);
    config.is_master = true;
    config.i2s_format = I2S_STD_FORMAT;
    config.pin_bck = PetConfig::I2S_BCLK_PIN;
    config.pin_ws = PetConfig::I2S_LRCK_PIN;
    config.pin_data = PetConfig::I2S_DAC_DATA_PIN;
    config.pin_data_rx = PetConfig::I2S_MIC_DATA_PIN;
    config.pin_mck = -1;

    if (!i2s_.begin(config)) {
        Serial.println("[Audio] ERROR: I2S RXTX begin failed");
        return false;
    }

    Serial.printf("[Audio] Ready: %lu Hz, stereo %u-bit I2S slots\n",
                  static_cast<unsigned long>(PetConfig::AUDIO_SAMPLE_RATE),
                  PetConfig::I2S_BITS_PER_SAMPLE);
    Serial.printf("[Audio] BCLK=D8/GPIO%d WS=D9/GPIO%d MIC=D10/GPIO%d DAC=D3/GPIO%d\n",
                  PetConfig::I2S_BCLK_PIN,
                  PetConfig::I2S_LRCK_PIN,
                  PetConfig::I2S_MIC_DATA_PIN,
                  PetConfig::I2S_DAC_DATA_PIN);
    return true;
}

int16_t AudioEngine::micWordToPcm16(int32_t word) const {
    const int64_t amplified =
        static_cast<int64_t>(word) * PetConfig::MIC_GAIN;

    int64_t pcm = amplified >> 16;
    if (pcm > INT16_MAX) pcm = INT16_MAX;
    if (pcm < INT16_MIN) pcm = INT16_MIN;
    return static_cast<int16_t>(pcm);
}

size_t AudioEngine::readMicPcm16(int16_t *outSamples, size_t maxSamples) {
    if (outSamples == nullptr || maxSamples == 0) {
        return 0;
    }

    const size_t bytesRead = i2s_.readBytes(
        reinterpret_cast<uint8_t *>(rxWords_),
        sizeof(rxWords_));

    if (bytesRead == 0) {
        return 0;
    }

    const size_t wordCount = bytesRead / sizeof(int32_t);
    size_t written = 0;

    for (size_t i = 0;
         i + 1 < wordCount && written < maxSamples;
         i += PetConfig::I2S_CHANNELS) {

        const int32_t first = rxWords_[i];
        const int32_t second = rxWords_[i + 1];

        const int64_t magFirst =
            first < 0 ? -static_cast<int64_t>(first) : first;
        const int64_t magSecond =
            second < 0 ? -static_cast<int64_t>(second) : second;

        // Same robust slot selection as the tested loopback firmware.
        const int32_t micWord =
            magFirst >= magSecond ? first : second;

        outSamples[written++] = micWordToPcm16(micWord);
    }

    return written;
}

bool AudioEngine::playPcm16(const int16_t *samples, size_t sampleCount) {
    if (samples == nullptr || sampleCount == 0) {
        return true;
    }

    size_t offset = 0;

    while (offset < sampleCount) {
        const size_t frames = min(
            static_cast<size_t>(PetConfig::I2S_FRAMES_PER_BLOCK),
            sampleCount - offset);

        for (size_t i = 0; i < frames; ++i) {
            const int32_t expanded =
                static_cast<int32_t>(samples[offset + i]) << 16;

            txWords_[i * 2] = expanded;
            txWords_[i * 2 + 1] = expanded;
        }

        const size_t bytes =
            frames * PetConfig::I2S_CHANNELS * sizeof(int32_t);

        const size_t written = i2s_.write(
            reinterpret_cast<const uint8_t *>(txWords_),
            bytes);

        if (written != bytes) {
            Serial.printf("[Audio] Speaker underrun/write error: %u/%u bytes\n",
                          static_cast<unsigned>(written),
                          static_cast<unsigned>(bytes));
            return false;
        }

        offset += frames;
    }

    return true;
}
