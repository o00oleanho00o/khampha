#include "AudioManager.h"

#include <limits.h>
#include <esp_heap_caps.h>

AudioManager::AudioManager()
    : audioInfo_(AppConfig::AUDIO_SAMPLE_RATE,
                 AppConfig::AUDIO_CHANNELS,
                 AppConfig::AUDIO_BITS_PER_SAMPLE) {}

AudioManager::~AudioManager() {
    if (recordBuffer_ != nullptr) {
        free(recordBuffer_);
        recordBuffer_ = nullptr;
    }
}

bool AudioManager::begin() {
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

    Serial.printf("[Audio] PSRAM found: %s\n", psramFound() ? "yes" : "no");

    const size_t bytesNeeded =
        AppConfig::MAX_RECORD_SAMPLES * sizeof(int16_t);

    recordBuffer_ = static_cast<int16_t *>(
        heap_caps_malloc(bytesNeeded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (recordBuffer_ == nullptr) {
        // Fallback for board configurations where PSRAM is unavailable.
        recordBuffer_ = static_cast<int16_t *>(malloc(bytesNeeded));
    }

    if (recordBuffer_ == nullptr) {
        Serial.printf("[Audio] Cannot allocate %u bytes for recording buffer\n",
                      static_cast<unsigned>(bytesNeeded));
        return false;
    }

    auto config = i2s_.defaultConfig(RXTX_MODE);
    config.copyFrom(audioInfo_);
    config.is_master = true;
    config.i2s_format = I2S_STD_FORMAT;
    config.pin_bck = AppConfig::I2S_BCLK_PIN;
    config.pin_ws = AppConfig::I2S_LRCK_PIN;
    config.pin_data = AppConfig::I2S_DAC_DATA_PIN;
    config.pin_data_rx = AppConfig::I2S_MIC_DATA_PIN;
    config.pin_mck = -1;

    if (!i2s_.begin(config)) {
        Serial.println("[Audio] ERROR: failed to initialize I2S RXTX");
        return false;
    }

    Serial.println("[Audio] I2S ready");
    Serial.printf("[Audio] %lu Hz, %u ch, %u-bit slots\n",
                  static_cast<unsigned long>(AppConfig::AUDIO_SAMPLE_RATE),
                  AppConfig::AUDIO_CHANNELS,
                  AppConfig::AUDIO_BITS_PER_SAMPLE);
    Serial.printf("[Audio] BCLK=D8/GPIO%d WS=D9/GPIO%d MIC=D10/GPIO%d DAC=D3/GPIO%d\n",
                  AppConfig::I2S_BCLK_PIN,
                  AppConfig::I2S_LRCK_PIN,
                  AppConfig::I2S_MIC_DATA_PIN,
                  AppConfig::I2S_DAC_DATA_PIN);

    return true;
}

int16_t AudioManager::convertMicSampleToPcm16(int32_t sample) const {
    // INMP441 produces 24-bit audio in a 32-bit I2S slot. The useful bits
    // are effectively in the high part of the word on ESP32 I2S.
    const int64_t amplified =
        static_cast<int64_t>(sample) * AppConfig::MIC_GAIN;

    int64_t pcm16 = amplified >> 16;

    if (pcm16 > INT16_MAX) {
        pcm16 = INT16_MAX;
    } else if (pcm16 < INT16_MIN) {
        pcm16 = INT16_MIN;
    }

    return static_cast<int16_t>(pcm16);
}

size_t AudioManager::recordFixed(uint32_t seconds) {
    if (recordBuffer_ == nullptr || seconds == 0) {
        return 0;
    }

    const size_t targetSamples = min(
        static_cast<size_t>(seconds) * AppConfig::AUDIO_SAMPLE_RATE,
        AppConfig::MAX_RECORD_SAMPLES);

    recordedSamples_ = 0;

    Serial.println();
    Serial.printf("[MIC] Recording %lu second(s)... SPEAK NOW\n",
                  static_cast<unsigned long>(seconds));

    while (recordedSamples_ < targetSamples) {
        const size_t bytesRead = i2s_.readBytes(
            reinterpret_cast<uint8_t *>(rxBuffer_),
            sizeof(rxBuffer_));

        if (bytesRead == 0) {
            delay(1);
            continue;
        }

        const size_t words = bytesRead / sizeof(int32_t);

        for (size_t i = 0;
             i + 1 < words && recordedSamples_ < targetSamples;
             i += AppConfig::AUDIO_CHANNELS) {

            const int32_t first = rxBuffer_[i];
            const int32_t second = rxBuffer_[i + 1];

            const int64_t firstMagnitude =
                first < 0 ? -static_cast<int64_t>(first) : first;
            const int64_t secondMagnitude =
                second < 0 ? -static_cast<int64_t>(second) : second;

            // Your existing loopback selected whichever slot has the larger
            // signal so it remains robust to left/right slot ordering.
            const int32_t micSample =
                firstMagnitude >= secondMagnitude ? first : second;

            recordBuffer_[recordedSamples_++] =
                convertMicSampleToPcm16(micSample);
        }
    }

    Serial.printf("[MIC] Done. samples=%u bytes=%u\n",
                  static_cast<unsigned>(recordedSamples_),
                  static_cast<unsigned>(recordedSamples_ * sizeof(int16_t)));

    return recordedSamples_;
}

const int16_t *AudioManager::recordingData() const {
    return recordBuffer_;
}

size_t AudioManager::recordingSamples() const {
    return recordedSamples_;
}

uint32_t AudioManager::sampleRate() const {
    return AppConfig::AUDIO_SAMPLE_RATE;
}

void AudioManager::beginPcmPlayback() {
    hasPendingPlaybackByte_ = false;
    pendingPlaybackByte_ = 0;
    Serial.println("[Speaker] Playing TTS...");
}

bool AudioManager::writeStereoFrames(const int32_t *frames,
                                     size_t frameCount) {
    if (frameCount == 0) {
        return true;
    }

    const size_t bytes =
        frameCount * AppConfig::AUDIO_CHANNELS * sizeof(int32_t);

    const size_t written = i2s_.write(
        reinterpret_cast<const uint8_t *>(frames),
        bytes);

    return written == bytes;
}

bool AudioManager::playPcm16Bytes(const uint8_t *data,
                                  size_t byteCount) {
    if (data == nullptr || byteCount == 0) {
        return true;
    }

    size_t index = 0;
    size_t txFrames = 0;

    auto pushSample = [&](int16_t sample) -> bool {
        const int32_t expanded = static_cast<int32_t>(sample) << 16;

        txBuffer_[txFrames * 2] = expanded;
        txBuffer_[txFrames * 2 + 1] = expanded;
        ++txFrames;

        if (txFrames >= AppConfig::I2S_FRAMES_PER_BLOCK) {
            if (!writeStereoFrames(txBuffer_, txFrames)) {
                return false;
            }
            txFrames = 0;
        }
        return true;
    };

    if (hasPendingPlaybackByte_) {
        const uint16_t raw =
            static_cast<uint16_t>(pendingPlaybackByte_) |
            (static_cast<uint16_t>(data[0]) << 8);

        if (!pushSample(static_cast<int16_t>(raw))) {
            return false;
        }

        hasPendingPlaybackByte_ = false;
        index = 1;
    }

    while (index + 1 < byteCount) {
        const uint16_t raw =
            static_cast<uint16_t>(data[index]) |
            (static_cast<uint16_t>(data[index + 1]) << 8);

        if (!pushSample(static_cast<int16_t>(raw))) {
            return false;
        }

        index += 2;
    }

    if (index < byteCount) {
        pendingPlaybackByte_ = data[index];
        hasPendingPlaybackByte_ = true;
    }

    if (txFrames > 0) {
        if (!writeStereoFrames(txBuffer_, txFrames)) {
            return false;
        }
    }

    return true;
}

void AudioManager::endPcmPlayback() {
    // A valid PCM16 stream should always end on an even byte boundary.
    if (hasPendingPlaybackByte_) {
        Serial.println("[Speaker] Warning: odd final PCM byte discarded");
    }

    hasPendingPlaybackByte_ = false;
    pendingPlaybackByte_ = 0;
    Serial.println("[Speaker] Done");
}
