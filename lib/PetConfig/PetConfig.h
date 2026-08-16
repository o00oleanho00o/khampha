#pragma once

#include <Arduino.h>

namespace PetConfig {

// ============================================================
// OPENAI REALTIME
// ============================================================
constexpr const char *OPENAI_HOST = "api.openai.com";
constexpr uint16_t OPENAI_PORT = 443;

// Lower-cost/faster realtime model for the first hardware prototype.
// Change to "gpt-realtime-2.1" if you want the stronger model.
constexpr const char *REALTIME_MODEL = "gpt-realtime-2.1-mini";
constexpr const char *REALTIME_VOICE = "marin";

// Keep voice replies short for a physical companion and to control cost.
constexpr uint16_t MAX_OUTPUT_TOKENS = 500;

// ============================================================
// AUDIO
// OpenAI Realtime PCM input/output is 24 kHz PCM16 mono.
// The physical I2S bus remains stereo 32-bit because the INMP441 and
// PCM5102A are already wired/tested that way. We convert at the edges.
// ============================================================
constexpr uint32_t AUDIO_SAMPLE_RATE = 24000;
constexpr uint8_t I2S_CHANNELS = 2;
constexpr uint8_t I2S_BITS_PER_SAMPLE = 32;

// Existing tested wiring.
constexpr int I2S_BCLK_PIN = D8;
constexpr int I2S_LRCK_PIN = D9;
constexpr int I2S_MIC_DATA_PIN = D10;
constexpr int I2S_DAC_DATA_PIN = D3;

// INMP441 gain before reducing 32-bit I2S slots to PCM16.
constexpr int32_t MIC_GAIN = 4;

// I2S read/write granularity. 128 frames ~= 5.3 ms at 24 kHz.
constexpr size_t I2S_FRAMES_PER_BLOCK = 128;

// Send mic audio to OpenAI in 40 ms chunks.
constexpr size_t MIC_PACKET_SAMPLES = 960;

// Play returned audio in small 10 ms blocks so websocket processing
// remains responsive.
constexpr size_t PLAYBACK_BLOCK_SAMPLES = 240;

// Buffer up to 8 seconds of model audio in PSRAM.
constexpr size_t AUDIO_RING_SECONDS = 8;
constexpr size_t AUDIO_RING_SAMPLES =
    static_cast<size_t>(AUDIO_SAMPLE_RATE) * AUDIO_RING_SECONDS;

// Maximum decoded bytes accepted from one response.output_audio.delta.
constexpr size_t MAX_AUDIO_DELTA_BYTES = 64 * 1024;

// After the speaker finishes, discard a short amount of mic audio to
// remove I2S backlog and the acoustic tail. This is not AEC.
constexpr uint32_t MIC_REOPEN_DELAY_MS = 180;

// ============================================================
// SERVER VAD
// ============================================================
constexpr float VAD_THRESHOLD = 0.50f;
constexpr uint16_t VAD_PREFIX_PADDING_MS = 300;
constexpr uint16_t VAD_SILENCE_DURATION_MS = 550;

// ============================================================
// DEVICE
// Matches the newest serial firmware supplied in this project.
// XIAO USER LED is treated as active-low.
// ============================================================
constexpr int LED_PIN = 1;

// ============================================================
// NETWORK
// ============================================================
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WS_RECONNECT_MS = 3000;

}  // namespace PetConfig
