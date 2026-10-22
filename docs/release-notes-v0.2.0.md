# YouTube Chat TTS v0.2.0

Second release of the Flatpak OBS Studio extension. It keeps the espeak-ng
engine and adds a bundled offline neural Piper voice, with engine-aware
controls and UI fixes.

## Highlights

- **Piper neural voice:** bundled Piper 1.7.0 with ONNX Runtime 1.22.0 and the
  public-domain `en_US-ljspeech-medium` voice. Piper is CPU-only, offline, and
  loads its model once.
- **Engine-aware Voice controls:** Piper exposes a Speed slider (0.00x-3.00x);
  espeak-ng exposes Rate, Pitch and Amplitude. Only the selected engine's
  controls are shown.
- **UI fixes:** collapsed Queue and Log sections by default, advanced voice
  settings hidden by default, full multi-word locale labels, and corrected
  Piper speed direction (lower values speak more slowly).
- **Runtime isolation:** bundled ONNX Runtime uses a unique SONAME
  (`libonnxruntime-ytc.so.1`) to avoid collisions with other OBS extensions.
- **Chat providers:** official YouTube Data API v3 and experimental keyless
  InnerTube, with Auto fallback.
- **Filtering, queue, and persisted settings** from v0.1.0 remain supported.

## Easy install (prebuilt Flatpak bundle)

Download `OBS-Youtube-Chat-TTS-0.2.0.flatpak` from the release assets, then run:

```bash
flatpak install --user ./OBS-Youtube-Chat-TTS-0.2.0.flatpak
```

Then restart OBS and open **Docks → YouTube Chat TTS**. Add the
**YouTube Chat TTS** audio source before going live.

If Flatpak asks for the OBS runtime, install OBS first:

```bash
flatpak install flathub com.obsproject.Studio
```

## Build from source

```bash
flatpak install flathub org.freedesktop.Sdk//25.08
flatpak install flathub org.flatpak.Builder
./scripts/build-flatpak.sh
./scripts/smoke-flatpak.sh
```

## Verification

- Flatpak build and smoke test: passed
- Bundled espeak-ng sandbox test: passed
- Bundled Piper C API and backend probe: passed
- Host unit tests: updated for Piper speed mapping and settings
- Live OBS checks: dock loading, engine switching, Piper Speed, Test Voice
  audio meter, and espeak advanced controls verified

## Known limitations

- The extension is x86-64 only.
- Only one Piper voice is bundled: `en_US-ljspeech-medium`.
- Pitch and amplitude controls apply to espeak-ng only.
- Official `streamList` access for chats not owned by the API key is not claimed
  as verified.
- InnerTube is unofficial/experimental and may break without notice.
