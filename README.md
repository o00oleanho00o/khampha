# Pet AI Realtime V1.6 ESP32 WebSocket fix

Key fixes:
- Replaced `links2004/WebSockets` after OpenAI rejected its ESP32-S3 client frames with WebSocket close code `1002`.
- Bundled `ArduinoWebsockets` and fixed its ESP32 `setInsecure()` bridge so WSS can initialize in this lab prototype.
- Queue session configuration writes until the receive callback has returned.
- Fixed the microphone Base64 output buffer size (`mbedtls` error `-42`).


## Build note (V1.2)

`NetworkManager` and `RealtimeClient` are local PlatformIO libraries under `lib/`.
They both include `secrets.h`, which lives in the project-level `include/` folder.
V1.2 explicitly adds `-Iinclude` to `build_flags` so those local libraries can resolve the header.
It also compiles as GNU C++17 because the current `audio-tools` dependency uses C++17 inline variables.

# IMPORTANT — clean upgrade to Realtime V1.1

Do **not** extract this project over the older `pet-ai-voice-v1` folder. The old project uses `AppConfig.h`; this Realtime project uses `PetConfig.h`. Mixing the two produces `AppConfig.h: No such file or directory`.

Recommended: extract this ZIP to a brand-new folder, open that folder in VS Code, then run **PlatformIO: Clean** once before Build/Upload.

This project uses `lib_ldf_mode = chain` and keeps the patched WebSocket client under `lib/ArduinoWebsockets/` so clean builds do not depend on a modified PlatformIO cache.

---

# Pet AI — Realtime Voice V1

This version replaces the old three-request pipeline:

`Mic -> STT HTTP -> Responses HTTP -> TTS HTTP -> Speaker`

with one persistent OpenAI Realtime WebSocket session:

`INMP441 -> PCM16 stream -> OpenAI Realtime -> PCM16 stream -> PCM5102A`

The model also keeps conversation state inside the Realtime session and can call the local `set_led` function.

## Current behavior

- 24 kHz audio end-to-end.
- Microphone is streamed in ~40 ms PCM16 chunks.
- OpenAI server VAD automatically detects speech start/stop.
- Model audio is played as it arrives; it is not first converted to text and then sent to a separate TTS endpoint.
- `set_led(state)` executes locally on the ESP32 and the result is returned to the same Realtime conversation.
- Serial text can still be sent for debugging.
- Conversation is reset by reconnecting the Realtime session.

### Important: not full-duplex yet

There is no acoustic echo cancellation (AEC) yet. If the microphone remained live while the speaker played, the robot could hear its own voice. Therefore V1 pauses mic upload during an assistant turn and reopens it shortly after playback finishes.

This is already Realtime speech-to-speech (persistent WebSocket + streaming input/output + VAD), but it intentionally does **not** support reliable barge-in yet. AEC is the next step for true full-duplex conversation.

## Wiring

Same wiring as the tested I2S loopback:

- XIAO `D8/GPIO7` -> INMP441 SCK + PCM5102A BCK
- XIAO `D9/GPIO8` -> INMP441 WS + PCM5102A LRCK
- INMP441 SD -> XIAO `D10/GPIO9`
- PCM5102A DIN -> XIAO `D3/GPIO4`
- INMP441 L/R -> GND
- INMP441 VDD -> 3V3
- PCM5102A SCK -> GND
- All grounds common

## Configure

Edit:

`include/secrets.h`

```cpp
#define WIFI_SSID       "YOUR_WIFI"
#define WIFI_PASSWORD   "YOUR_PASSWORD"
#define OPENAI_API_KEY  "sk-..."
```

Never commit `secrets.h`.

## Build / run

1. Open this folder in VS Code + PlatformIO.
2. Build.
3. Upload to `seeed_xiao_esp32s3`.
4. Open Serial Monitor at `115200`.
5. Wait for:

```text
[Realtime] Session ready. Speak normally.
```

6. Speak. No `/talk` command is required.

Expected flow:

```text
[VAD] Speech started
[VAD] Speech stopped -> AI responding
[AI] Response started
AI> Xin chào! Tôi nghe thấy bạn rồi.
[AI] Playback done -> listening again
```

Tool test:

Say in Vietnamese:

`Bật đèn lên.`

Expected:

```text
[Tool] set_led({"state":true})
[Device] LED = ON
[Tool] Result: {"success":true,"led":"on"}
```

## Serial commands

- `/status`
- `/mute`
- `/unmute`
- `/reset`
- `/help`
- Any other text is sent as a user text message through the same Realtime session.

## Model

Default:

`gpt-realtime-2.1-mini`

It is selected for the first embedded prototype because it is faster and substantially cheaper than the full `gpt-realtime-2.1` model. Change `REALTIME_MODEL` in `lib/PetConfig/PetConfig.h` if desired.

## Security

For a personal lab prototype this firmware connects directly to OpenAI with a standard API key embedded in firmware. Do not ship this architecture in a commercial device: a production device should authenticate to your own backend and should not expose the long-lived OpenAI API key.

This prototype also uses the Arduino WebSockets ESP32 insecure TLS mode because no CA bundle is provided. Add certificate validation before production use.

## Current scope

This Realtime V1 keeps the local `set_led` function tool, but it does **not** carry the old Responses API built-in `web_search` into the realtime voice path yet. That can be added next as a custom tool bridge without changing the microphone/speaker streaming architecture.

## What changed versus Voice V1

Removed from the realtime path:

- `/v1/audio/transcriptions`
- `/v1/responses`
- `/v1/audio/speech`
- WAV recording buffer
- fixed 4-second recording turns
- `previous_response_id`

Replaced by:

- `wss://api.openai.com/v1/realtime?...`
- `input_audio_buffer.append`
- server VAD
- `response.output_audio.delta`
- Realtime conversation state
- Realtime function calling

## Next upgrade

For natural interruptions/full duplex, add:

1. AEC with the exact speaker PCM as the far-end reference.
2. A short adaptive delay estimator between DAC playback and microphone echo.
3. Keep mic upload active while the model speaks.
4. Enable `interrupt_response: true`.
5. On user barge-in, stop queued local playback and synchronize truncation/cancel events with the Realtime conversation.
