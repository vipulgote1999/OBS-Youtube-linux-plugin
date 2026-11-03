# Changelog

All notable changes to this project are documented here.

## [0.3.0] - 2026-11-03

### Added

- Bundled **Supertonic 3** in-process ONNX Runtime engine with ten voice
  styles (`F1`–`F5`, `M1`–`M5`), 4–16 denoising steps and offline inference.
- `supertonic-probe` packaging/latency/cancellation probe and Flatpak test.
- `AudioConsumerArbiter`, so only one active OBS source drains the shared TTS
  buffer when duplicate `YouTube Chat TTS` sources exist.
- Supertonic evaluation document, benchmark artifacts and blind listening page.
- OBS plugin submission guidelines and a ready-to-paste resource description.

### Changed

- Supertonic is now the default engine. Speed is clamped to the natural
  0.90–1.50× range; a saved `0.5` migrates to `0.9`.
- Flatpak bundles ONNX Runtime directly instead of shipping it through Piper.
- Version bumped to 0.3.0; AppStream metadata and README updated.

### Removed

- **Piper** engine, `libpiper`, the LJSpeech voice model and config.
- **espeak-ng** engine, executable, data, settings and UI controls.
- Piper/espeak probes, tests, Flatpak modules, licenses and third-party notices.
- The automatic espeak-ng fallback. A missing or corrupt Supertonic model now
  stops the worker with a readable error.

### Fixed

- Fast/choppy OBS speech caused by multiple `YouTube Chat TTS` sources each
  consuming the shared audio buffer independently.
- Supertonic streaming validates the backend sample rate before emitting audio.
- Missing upstream MIT notice for the ported Supertonic C++ inference code.

### Breaking changes

- Existing `"piper"` and `"espeak"` engine selections migrate to
  `"supertonic"`.
- Removed settings keys (`piper*`, `voice`, `rate`, `pitch`, `amplitude`,
  `espeakPath`) are ignored and dropped on the next save.
- Piper and espeak-ng are no longer available as fallback engines.

## [0.2.0] - 2026-10-22

### Added

- Bundled Piper 1.7.0 with ONNX Runtime 1.22.0 and the public-domain
  `en_US-ljspeech-medium` voice.
- Local OpenAI-compatible TTS server engine (Kokoro/Pocket TTS).
- Low-latency Piper streaming and `SpeechConditioner` fades/DC blocking.
- Piper speed control, engine-aware controls and UI fixes.

### Fixed

- Stretched Piper voice when a saved speed was `0.00`.
- ONNX Runtime SONAME isolation from other OBS extensions.

## [0.1.0] - 2026-10-21

### Added

- First release: YouTube live-chat dock, official API and experimental
  InnerTube providers, bundled espeak-ng, native OBS audio source, filtering,
  queue, persisted settings and Flatpak packaging.

[0.3.0]: https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/releases/tag/v0.3.0
[0.2.0]: https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/releases/tag/v0.2.0
[0.1.0]: https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/releases/tag/v0.1.0
