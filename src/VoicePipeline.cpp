#include "VoicePipeline.h"

VoicePipeline::VoicePipeline(AudioManager &audio,
                             OpenAIClient &openAI,
                             Agent &agent)
    : audio_(audio), openAI_(openAI), agent_(agent) {}

bool VoicePipeline::runTurn(uint32_t recordSeconds) {
    const size_t sampleCount = audio_.recordFixed(recordSeconds);
    if (sampleCount == 0) {
        Serial.println("[Voice] Recording failed");
        return false;
    }

    Serial.println("[Voice] Speech-to-text...");

    const String transcript = openAI_.transcribePcm16(
        audio_.recordingData(),
        sampleCount,
        audio_.sampleRate());

    if (transcript.isEmpty()) {
        Serial.println("[Voice] Empty transcript");
        return false;
    }

    Serial.println();
    Serial.print("You> ");
    Serial.println(transcript);

    Serial.println("[Voice] Asking Pet AI...");
    const String answer = agent_.ask(transcript);

    if (answer.isEmpty()) {
        Serial.println("[Voice] AI returned no speakable text");
        return false;
    }

    Serial.println("[Voice] Text-to-speech...");

    if (!openAI_.speak(answer, audio_)) {
        Serial.println("[Voice] TTS/playback failed");
        return false;
    }

    return true;
}
