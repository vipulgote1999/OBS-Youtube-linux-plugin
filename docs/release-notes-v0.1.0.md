# YouTube Chat TTS v0.1.0

First public release of the Flatpak OBS Studio extension that reads YouTube live chat aloud with the bundled offline espeak-ng engine and mixes the speech into OBS as a native audio source.

## Highlights

- **Two chat providers:** official YouTube `liveChatMessages.streamList` over gRPC (API key), and a no-key experimental InnerTube provider. `Auto` falls back to InnerTube when the official provider is unavailable.
- **Offline speech:** bundled espeak-ng 1.51, no Python or cloud TTS at runtime.
- **Native OBS audio:** 22,050 Hz mono float audio source with a 50 ms feeder; OBS handles resampling and mixing.
- **Filtering and control:** message filtering, per-user and global cooldowns, URL/emoji/empty-message handling, blocklist, queue limit, Super Chat priority, membership/moderator toggles, and skip/clear controls.
- **Full dock:** provider/API key setup, voice presets and sliders, queue preview, log, status/error mapping, API-key link, and audio-source creation.
- **Safe defaults:** API key stored in `settings.json` with mode `0600`, passed to the helper over stdin, never argv or logs.
- **Flatpak extension:** `com.obsproject.Studio.Plugin.YtChatTts` for the OBS Studio Flatpak, with a statically linked Go helper and vendored third-party notices.

## Install / build

The supported v1 target is OBS Studio Flatpak. See the repository README for prerequisites and the full build/install guide:

```bash
flatpak install flathub org.freedesktop.Sdk//25.08
flatpak install flathub org.flatpak.Builder
./scripts/build-flatpak.sh
./scripts/smoke-flatpak.sh
```

Then restart OBS and open **Docks → YouTube Chat TTS**. Add the **YouTube Chat TTS** audio source before going live.

## Important status: early release

This is an **early pre-release**. The code passes the headless host/Go tests, Flatpak build and smoke tests, bundled espeak-ng test, sandbox library checks, and a live helper-level InnerTube chat gate. The full interactive OBS frontend acceptance matrix — dock loading, official API key path, recording/Test Voice, settings persistence, process cleanup, and live voice/rate changes — is still **pending** because the maintainer's OBS session was active during the release build.

See [`docs/acceptance-checklist.md`](https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/blob/main/docs/acceptance-checklist.md) for the exact remaining checks. The release may be promoted from pre-release after that matrix passes.

Known limitations:

- Official `streamList` access for chats not owned by the API key is unverified.
- InnerTube is unofficial/experimental and may break without notice; chat replay is intentionally unsupported.
- Helper start/stop is synchronous on the OBS UI thread; a hung helper can briefly delay OBS.
- The extension is x86-64 only and requires a rebuild after OBS/runtime ABI changes.

## Verification at release

- Host CTest: 9/9 passed
- Go `test -count=1 ./...` and `go vet ./...`: passed
- Flatpak build and strengthened smoke test: passed
- Bundled espeak-ng sandbox test: passed
- Sandbox `ldd`: no missing libraries; helper reports `ytchat-helper 0.1.0`
- Live helper-level InnerTube: connecting → connected → real chat messages, no stderr

Full source is available in the GitHub archive for this tag. Project code is GPL-2.0-or-later; bundled espeak-ng is GPL-3.0-or-later as a separate executable. Third-party notices and license texts are in the repository and installed extension.
