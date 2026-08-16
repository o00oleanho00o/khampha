#pragma once

#include <Arduino.h>

#include "AudioManager.h"
#include "OpenAIClient.h"
#include "Agent.h"

class VoicePipeline {
public:
    VoicePipeline(AudioManager &audio,
                  OpenAIClient &openAI,
                  Agent &agent);

    // One complete half-duplex turn:
    // mic -> STT -> LLM/tools -> TTS -> speaker
    bool runTurn(uint32_t recordSeconds);

private:
    AudioManager &audio_;
    OpenAIClient &openAI_;
    Agent &agent_;
};
