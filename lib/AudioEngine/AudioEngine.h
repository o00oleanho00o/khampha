#pragma once

#include <Arduino.h>
#include "AudioTools.h"
#include "PetConfig.h"

class AudioEngine {
public:
    AudioEngine();

    bool begin();

    // Read one small I2S block from INMP441 and convert it to mono PCM16.
    // Returns the number of PCM16 mono samples written to outSamples.
    size_t readMicPcm16(int16_t *outSamples, size_t maxSamples);

    // Play mono PCM16 24 kHz by expanding each sample to both 32-bit
    // I2S L/R slots for the PCM5102A.
    bool playPcm16(const int16_t *samples, size_t sampleCount);

private:
    AudioInfo audioInfo_;
    I2SStream i2s_;

    int32_t rxWords_[PetConfig::I2S_FRAMES_PER_BLOCK * PetConfig::I2S_CHANNELS];
    int32_t txWords_[PetConfig::I2S_FRAMES_PER_BLOCK * PetConfig::I2S_CHANNELS];

    int16_t micWordToPcm16(int32_t word) const;
};
