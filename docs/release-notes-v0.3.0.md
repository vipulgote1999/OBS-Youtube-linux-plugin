# YouTube Chat TTS v0.3.0

Third release of the Flatpak OBS Studio extension. Supertonic 3 is now the
bundled engine, the duplicate-source audio bug is fixed, and **Piper and
espeak-ng have been removed**.

> This is a breaking release. Existing `"piper"` and `"espeak"` engine
> selections migrate to `"supertonic"` automatically; the removed controls and
> settings keys no longer exist.

## Highlights

- **Supertonic 3, fully offline:** in-process ONNX Runtime inference with ten
  bundled voice styles (`F1`–`F5`, `M1`–`M5`), adjustable denoising steps
  (4–16) and natural 0.90–1.50× speed. No Python, no cloud TTS, no runtime
  network access.
- **Fixed fast/choppy OBS audio:** duplicate `YouTube Chat TTS` sources were
  each draining the same audio buffer, so four sources consumed speech about
  4× faster than real time. `AudioConsumerArbiter` now lets exactly one active
  source consume; duplicates emit silence and are logged.
- **Better default pace:** `0.5×` is no longer exposed because the model spends
  that extra duration on long pauses. A saved `0.5` migrates to `0.9`.
- **Piper and espeak-ng removed:** their code, models, data, probes, tests,
  licenses and Flatpak modules are gone. The local OpenAI-compatible server
  engine remains as the alternative.
- **No automatic fallback:** a missing or corrupt Supertonic model stops the
  worker with a readable error instead of speaking through another engine.
- **Smaller extension:** ONNX Runtime is bundled directly; the installed
  extension is **446 MB** instead of 547 MB.

## Install (prebuilt Flatpak bundle)

Download `OBS-Youtube-Chat-TTS-0.3.0.flatpak` from the release assets, then run:

```bash
flatpak install --user --bundle ./OBS-Youtube-Chat-TTS-0.3.0.flatpak
```

Or download and install in two commands:

```bash
wget https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/releases/download/v0.3.0/OBS-Youtube-Chat-TTS-0.3.0.flatpak
flatpak install --user --bundle ./OBS-Youtube-Chat-TTS-0.3.0.flatpak
```

SHA-256:

```text
8134d0c5ba990f5edb7f649dcafc3073e4f743c52ffcfdf79166aabee15b68b6
```

If Flatpak asks for the OBS runtime, install OBS first:

```bash
flatpak install flathub com.obsproject.Studio
```

Then restart OBS and open **Docks → YouTube Chat TTS**. Add the
**YouTube Chat TTS** audio source before going live.

## Upgrade notes

- Start OBS after installing; the extension is loaded at startup.
- Your engine setting becomes **Supertonic** if it was `piper` or `espeak`.
- Supertonic voice defaults to `F1`; speed to `1.00×`; steps to `8`.
- Your existing volume, filtering, queue and chat-provider settings are kept.
- If the dock reports a missing model, reinstall the bundle and run
  `./scripts/smoke-flatpak.sh` from a source checkout.

## Build from source

```bash
flatpak install flathub org.freedesktop.Sdk//25.08
flatpak install flathub org.flatpak.Builder
./scripts/build-helper.sh
./scripts/build-flatpak.sh
./scripts/smoke-flatpak.sh
./scripts/test-supertonic-flatpak.sh
```

## Verification

- Host suite: **14/14 passing** with and without ONNX Runtime.
- Flatpak build and `scripts/smoke-flatpak.sh`: passed.
- `scripts/test-supertonic-flatpak.sh`: passed (synthesis, 200-character
  benchmark, cancellation, missing-model error path).
- Bundle installed from `OBS-Youtube-Chat-TTS-0.3.0.flatpak`; smoke and
  Supertonic tests passed again after installation.
- Installed extension loads in OBS 32.2.2 as `0.3.0`; dock shows only
  Supertonic and Local server.
- OBS log confirms one active TTS consumer and logs duplicate sources as
  silent.

## Known limitations

- Linux x86-64 Flatpak only. The extension requires OBS Studio from Flathub.
- Only the ten built-in Supertonic voices are supported.
- Number/currency symbols and CJK text are unreliable without a language or
  number normalizer; the engine is pinned to the `en` language tag.
- No automatic fallback engine.
- Official `streamList` access for chats not owned by the API key is not
  claimed as verified.
- InnerTube is unofficial/experimental and may break without notice.

## Development disclosure

This project was developed with AI coding assistants and reviewed, tested and
packaged by the maintainer. AI tools were used for implementation, test
authoring, documentation drafts and audio-quality review; every change was
compiled and exercised through the host, Flatpak and OBS checks listed above.
This disclosure is provided for the OBS plugin resource submission policy.

## License

Project code is GPL-2.0-or-later. Third-party notices, including ONNX Runtime
and the Supertonic model/code licenses, are in
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
