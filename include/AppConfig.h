#pragma once

#include <Arduino.h>

namespace AppConfig {

// -----------------------------
// OpenAI
// -----------------------------
constexpr const char *OPENAI_HOST = "api.openai.com";
constexpr uint16_t OPENAI_PORT = 443;
constexpr const char *RESPONSES_PATH = "/v1/responses";
constexpr const char *TRANSCRIPTIONS_PATH = "/v1/audio/transcriptions";
constexpr const char *SPEECH_PATH = "/v1/audio/speech";

// Keep the LLM model that is already working in your current project.
constexpr const char *LLM_MODEL = "gpt-5-mini";

// Current recommended file-transcription model from OpenAI docs.
constexpr const char *STT_MODEL = "gpt-transcribe";

// Low-latency TTS model. We request raw PCM output.
constexpr const char *TTS_MODEL = "gpt-4o-mini-tts";
constexpr const char *TTS_VOICE = "coral";

// -----------------------------
// Audio
// -----------------------------
// OpenAI PCM TTS is 24 kHz, signed 16-bit, little-endian.
// Running the I2S bus at 24 kHz lets us play it without resampling.
constexpr uint32_t AUDIO_SAMPLE_RATE = 24000;
constexpr uint8_t AUDIO_CHANNELS = 2;
constexpr uint8_t AUDIO_BITS_PER_SAMPLE = 32;

// Existing wiring from your tested I2S loopback sketch.
constexpr int I2S_BCLK_PIN = D8;
constexpr int I2S_LRCK_PIN = D9;
constexpr int I2S_MIC_DATA_PIN = D10;
constexpr int I2S_DAC_DATA_PIN = D3;

// INMP441 24-bit samples arrive in 32-bit I2S slots.
// Increase if the transcript is too quiet; decrease if clipping occurs.
constexpr int32_t MIC_GAIN = 4;

// Stable V1 behavior: type /talk, then speak for this many seconds.
constexpr uint32_t RECORD_SECONDS = 4;

// Allocate enough room to optionally record longer turns later.
constexpr uint32_t MAX_RECORD_SECONDS = 10;
constexpr size_t MAX_RECORD_SAMPLES =
    static_cast<size_t>(AUDIO_SAMPLE_RATE) * MAX_RECORD_SECONDS;

constexpr size_t I2S_FRAMES_PER_BLOCK = 128;

// -----------------------------
// Device
// -----------------------------
// This matches the newest firmware you uploaded, where the onboard LED
// is driven through pin 1 and is active-low.
constexpr int LED_PIN = 1;

// -----------------------------
// Networking
// -----------------------------
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t HTTP_TIMEOUT_MS = 90000;

}  // namespace AppConfig
