# Piper TTS Engine Design

Date: 2026-10-21
Status: Draft for review
Project: OBS YouTube Chat TTS

## Goal

Add a natural-sounding, offline Piper speech engine to the existing OBS chat TTS
pipeline while keeping runtime CPU and memory use low. espeak-ng remains the
fallback. No Python is embedded in OBS and no cloud TTS is used.

The user-facing log line `tts: speaking: @author` stays a status message; it is
not an error. The author handle is spoken only when "read author" is enabled.

## Decisions

- **Engine:** Piper via the `libpiper` C API, not a Piper CLI process per
  message. The voice model is loaded once per worker and reused.
- **Voice:** `en_US-ljspeech-medium` (US female, 22,050 Hz, public-domain
  LJSpeech dataset). The previously suggested `en_US-lessac-medium` is rejected
  because its Lessac/Blizzard dataset license is research-only and cannot be
  bundled in a public release.
- **CPU/GPU:** CPU-only ONNX Runtime. No GPU, CUDA, cloud, or network
  dependency at runtime.
- **Packaging:** bundle Piper, ONNX Runtime, the model, and all licenses in the
  Flatpak extension. Roughly 63 MB of model data plus the Piper/ONNX runtime
  libraries; expected extension growth around 90–140 MB.
- **Fallback:** if Piper is unavailable or cannot load, use espeak-ng and log a
  clear message. A failed Piper synthesis reports an error and continues with
  later messages; it does not silently switch engines mid-utterance.
- **Audio contract:** Piper returns float32 mono at 22,050 Hz. No resampling is
  expected for this voice; if a future model reports another rate, it is
  rejected with a descriptive error.

## UI

Add an **Engine** selector to the Voice group:

- `Piper — natural (CPU)` — default when the bundled model/runtime is present.
- `espeak-ng — fast fallback` — existing engine.

The existing Voice control becomes engine-aware:

- Piper: one bundled voice, `LJSpeech — US female (medium)`.
- espeak-ng: existing selectable voices.

Controls and behavior:

- **Rate** maps to Piper `length_scale` (`lengthScale = 165 / rate`, clamped
  to `0.5–2.0`). Higher OBS rate means faster speech.
- **Pitch** and **Amplitude** are disabled/greyed for Piper with a tooltip
  explaining they are espeak-only.
- **Volume** remains active for both engines and stays owned by `AudioBuffer`.
- **Read author**, **Template**, **Max length**, **Test Voice**, filters, queue,
  status, and logs are unchanged.
- Log/status includes the active engine and voice once, for example
  `tts engine: piper (en_US-ljspeech-medium)` or `tts engine: espeak-ng`.
- If the user selects Piper but the bundled files are missing, the dock reports
  that the fallback is espeak; it never goes silent.

## Settings and migration

Add:

- `ttsEngine` — `piper` or `espeak`; default `piper` for new and existing
  settings. If Piper is unavailable, the runtime falls back to espeak and logs
  the reason; the setting remains `piper` so the user can fix the installation
  and use Piper again. Users can explicitly select espeak to avoid loading
  Piper.
- `piperVoice` — bundled voice id, initially `en_US-ljspeech-medium`.
- `piperModelPath` / `piperConfigPath` — optional overrides; empty means resolve
  the bundled paths.

Existing fields remain compatible. `voice`, `rate`, `pitch`, `amplitude` keep
their current meanings for espeak. No API key or helper changes.

## Architecture

```
ChatClient -> MessageFilter -> TtsQueue -> TtsWorker -> PiperEngine -> AudioBuffer
                                                      `-> EspeakEngine (fallback)
```

- Add `PiperEngine : TtsEngine` in `src/piper-engine.{hpp,cpp}`.
- `PiperEngine` holds a lazily initialized `piper_synthesizer*`. Initialization
  happens on the worker thread in the first `synthesize()` call, so the UI is
  not blocked by model loading.
- `synthesize()` calls `piper_synthesize_start()` and loops over
  `piper_synthesize_next()`, appending float samples; it checks an atomic
  cancellation flag between chunks. `cancel()` only sets the flag.
- The engine returns the model sample rate; the worker continues to require
  22,050 Hz.
- Do not modify `TtsQueue`, `AudioBuffer`, the OBS source, or the chat/filter
  path except for engine selection and the rate mapping.
- `PluginContext::ensureWorker()` resolves the Piper model/config and espeak
  data through the existing `paths.cpp` resolver. If any required path or
  `libpiper` initialization is unavailable, it creates `EspeakEngine` and logs
  the exact missing path.
- `TtsWorker` continues to own one speech thread. Piper synthesis runs only on
  that thread.

## Packaging

Add a dedicated `piper` Flatpak module built from pinned sources:

- `OHF-Voice/piper1-gpl` `libpiper` at a pinned tag/commit.
- ONNX Runtime CPU at a pinned version (prebuilt Linux x86-64 archive or a
  source build) supplied through `ONNXRUNTIME_DIR`; Flatpak builds must not
  perform network downloads during the build.
- Piper's required espeak-ng dependency/data. Prefer reusing the existing
  espeak-ng module where API-compatible; otherwise build the pinned espeak-ng
  inside the Piper module. Do not depend on the host's espeak.
- Voice model and config:
  `en_US-ljspeech-medium.onnx` and `.onnx.json`, pinned to a Hugging Face
  revision and SHA-256; install under the extension data directory.
- Install Piper/ONNX shared libraries where the OBS module can load them, and
  add `ldd`/smoke checks.
- Add GPL-3.0-or-later Piper notice/source, ONNX Runtime MIT notice, espeak-ng
  GPL notice, and the LJSpeech public-domain model card to
  `THIRD_PARTY_NOTICES.md` / `LICENSES/`.

The exact CMake patch for `libpiper`'s external espeak-ng download must be
included in the implementation plan; the build must be reproducible offline.

## Tests and acceptance

Host tests:

- Fake-engine worker tests continue to pass.
- Add `PiperEngine` unit coverage through a small injectable API wrapper or a
  fake synthesis helper; verify rate mapping, cancellation, and error paths
  without requiring ONNX Runtime on the host.
- Settings round-trip and migration tests for `ttsEngine`, `piperVoice`, and
  path overrides.

Flatpak/integration tests:

- A sandbox script synthesizes a short phrase with the real bundled Piper model
  and verifies non-empty, 22,050 Hz PCM.
- `smoke-flatpak.sh` checks Piper library, ONNX Runtime library, model/config,
  espeak data, and licenses, with no missing shared libraries.
- Measure first-load and steady-state synthesis time for a 200-character
  message on the reference CPU; target steady-state under 1.5 s and no
  per-message model reload.
- In OBS: Test Voice with Piper, then a live chat message; verify Audio Mixer
  level, queue behavior, voice/rate changes, fallback to espeak when Piper is
  unavailable, and no helper/espeak/Piper process leaks after exit.

## Out of scope

- Multiple bundled Piper voices; model downloads; GPU/CUDA; cloud TTS.
- Changing the chat provider, filtering, queue, OBS source, or recording path.
- Piper training or voice cloning.

## Risks

- `libpiper` packaging includes ONNX Runtime and a pinned espeak-ng; this is the
  main build risk. If the offline build cannot be made reproducible, a
  persistent Piper CLI is the fallback design, but it requires a new design
  approval because it has different CPU/latency/package tradeoffs.
- Extension size grows by roughly 100+ MB.
- GPL-3.0-or-later Piper library/executable must remain a separate component and
  be documented; the project's GPL-2.0-or-later code is GPL-3-compatible.
- Model output may be slower on very low-end CPUs; the acceptance run measures
  this rather than assuming it.
