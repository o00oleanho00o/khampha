# Pet AI Voice V1

Target hardware:

- Seeed Studio XIAO ESP32S3
- INMP441 I2S microphone
- PCM5102A I2S DAC
- Wi-Fi
- OpenAI API

Pipeline:

```text
INMP441 -> ESP32 -> WAV upload -> OpenAI STT
                         |
                         v
                    Responses API
                  chat/web/tools/LED
                         |
                         v
                   OpenAI TTS PCM
                         |
                         v
               ESP32 I2S -> PCM5102A
```

## Wiring

Shared I2S clocks:

```text
XIAO D8 / GPIO7  -> INMP441 SCK + PCM5102A BCK
XIAO D9 / GPIO8  -> INMP441 WS  + PCM5102A LRCK/LCK
```

Data:

```text
INMP441 SD       -> XIAO D10 / GPIO9
PCM5102A DIN     -> XIAO D3  / GPIO4
INMP441 L/R      -> GND
INMP441 VDD      -> 3V3
PCM5102A SCK     -> GND
All GNDs common
```

Important: PCM5102A is a DAC/line-output device. A passive loudspeaker needs an amplifier after the DAC.

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.h`.
2. Put your Wi-Fi SSID/password and OpenAI API key in `include/secrets.h`.
3. Build/upload with PlatformIO.
4. Open Serial Monitor at 115200.
5. Type `/talk`.
6. When `SPEAK NOW` appears, speak for 4 seconds.
7. The device uploads the WAV to STT, sends the transcript to the existing Responses/tool loop, requests PCM TTS, and plays it through PCM5102A.

You can also use `/talk 6` for a 6-second recording or send normal text over Serial as a fallback.

## Why 24 kHz?

OpenAI raw PCM TTS is 24 kHz signed 16-bit little-endian. The project runs the I2S bus at 24 kHz and expands each mono PCM16 TTS sample to stereo 32-bit I2S slots for PCM5102A, so no MP3 decoder or resampler is required.

## V1 limitations

This is intentionally half-duplex and push-to-talk for stability:

```text
record -> STT -> AI -> TTS -> playback -> ready
```

It does not yet do automatic VAD, AEC, barge-in, or full-duplex Realtime API audio. Those should be added after this pipeline is verified on the hardware.
