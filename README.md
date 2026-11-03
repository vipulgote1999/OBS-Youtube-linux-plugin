# YouTube Chat TTS

A Flatpak extension for OBS Studio that reads YouTube live chat aloud with the
bundled offline neural [Supertonic 3](https://huggingface.co/supertone-oss-archive/supertonic-3)
engine (CPU-only) and mixes the synthesized speech into OBS as a native audio
source. There is no cloud TTS, no virtual audio cable and no Python inside OBS.

The extension is a C++17/Qt 6 OBS module built inside the
`org.freedesktop.Sdk//25.08` sandbox and installed as
`com.obsproject.Studio.Plugin.YtChatTts`. A bundled Go helper talks to YouTube:
either through the official YouTube Data API v3 (API key required) or through
the experimental keyless InnerTube provider.

## Features

- **Dock** with connection state, the active provider, voice controls,
  filtering, the next queued messages and the last 500 log lines.
- **Official provider** using the YouTube Data API v3 for resolution and
  YouTube's server-streamed `StreamList` gRPC API for live chat.
- **Experimental no-key provider** (InnerTube) for public chats without a
  Google Cloud project.
- **Auto mode**: official first when a key is configured, with a visible
  warning and fallback to InnerTube on a pre-connection auth, quota or network
  failure.
- **Offline neural speech** through the bundled Supertonic 3 model and ONNX
  Runtime. Synthesis is CPU-only and the model is loaded once per worker, not
  per message.
- **Ten bundled voices** (`F1`–`F5`, `M1`–`M5`), adjustable speed
  (0.90–1.50×) and denoising steps (4–16).
- **Local high-quality engines**: point the plugin at an OpenAI-compatible TTS
  server you run (Kokoro, Pocket TTS, ...) to use a different voice without
  bundling another model.
- **Low-latency streaming**: each sentence is pushed to OBS as it is
  synthesized instead of after the whole message. Toggle it off in the dock.
- **Filtering**: max length, per-user and global cooldowns, command prefix,
  URL stripping, blocklisted phrases, Super Chats, new members, moderators.
- **Bounded queue** (default 25, configurable 1–100) with Skip Current and
  Clear Queue; overflow drops the lowest-priority/oldest message instead of
  growing without bound.
- **Owner-only settings** (`0600`) persisted automatically 500 ms after the
  last edit. The API key is never logged and never placed on a command line
  or in a process argument.

## Requirements

- Linux x86-64 with Flatpak 1.14 or newer.
- OBS Studio from Flathub (built against **32.2.2**; live acceptance pending,
  see [`docs/acceptance-checklist.md`](docs/acceptance-checklist.md)):
  ```bash
  flatpak install flathub com.obsproject.Studio
  ```
- The matching SDK and a Flatpak builder:
  ```bash
  flatpak install flathub org.freedesktop.Sdk//25.08
  flatpak install flathub org.flatpak.Builder
  # or install your distribution's flatpak-builder package
  ```
  The build script uses a host `flatpak-builder` when available and otherwise
  runs `org.flatpak.Builder`. Installing OBS also brings in the
  `com.obsproject.Studio//stable` runtime the extension builds against.
- **Go 1.22 or newer** to build the chat helper. Only building needs Go; the
  runtime does not.

The first SDK install is a large one-time download. The SDK is not vendored in
this repository.

The bundled engine needs no GPU or CUDA and no runtime network access. The
Supertonic 3 model and ten voices are about 381 MB, ONNX Runtime adds about
21 MB of shared libraries, and the installed extension is roughly 440–470 MB.
Those files are bundled so the extension stays offline and self-contained.

## Quick install (prebuilt Flatpak bundle)

Download `OBS-Youtube-Chat-TTS-0.3.0.flatpak` from the
[latest release](https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/releases/latest),
then install it with Flatpak:

```bash
flatpak install --user --bundle ./OBS-Youtube-Chat-TTS-0.3.0.flatpak
```

Or download and install in two commands:

```bash
wget https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/releases/download/v0.3.0/OBS-Youtube-Chat-TTS-0.3.0.flatpak
flatpak install --user --bundle ./OBS-Youtube-Chat-TTS-0.3.0.flatpak
```

Then restart OBS and open **Docks → YouTube Chat TTS**. Add the
**YouTube Chat TTS** audio source before going live.

The bundle is x86-64 only and requires OBS Studio from Flathub. If Flatpak
asks for the OBS runtime, install OBS first:

```bash
flatpak install flathub com.obsproject.Studio
```

SHA-256 for the bundle:
`8134d0c5ba990f5edb7f649dcafc3073e4f743c52ffcfdf79166aabee15b68b6`

Release notes and the full changelog are in
[`docs/release-notes-v0.3.0.md`](docs/release-notes-v0.3.0.md) and
[`CHANGELOG.md`](CHANGELOG.md).

## Build, install and verify

```bash
./scripts/build-helper.sh      # produces dist/ytchat-helper (static x86-64)
./scripts/build-flatpak.sh     # reruns build-helper.sh, then installs the extension
./scripts/smoke-flatpak.sh     # checks the installed extension inside the sandbox
./scripts/test-supertonic-flatpak.sh # Supertonic synthesis, benchmark, cancellation
```

`build-flatpak.sh` refuses to start the Flatpak build if `dist/ytchat-helper`
is missing after the helper build, because the extension must always bundle
the helper.

Clean rebuild (removes only generated build output, not the Flatpak download
cache or your OBS configuration):

```bash
rm -rf build build-flatpak dist
./scripts/build-helper.sh
./scripts/build-flatpak.sh
./scripts/smoke-flatpak.sh
./scripts/test-supertonic-flatpak.sh
./scripts/test-host.sh
```

`smoke-flatpak.sh` verifies, inside the OBS sandbox, that the module and
locale, the executable helper (`ytchat-helper --version`), the Supertonic 3
model with all ten voice styles, ONNX Runtime, the packaging probe, the
AppStream metainfo and the license/notice files are all present, that the
module actually links the bundled ONNX Runtime, and that no installed binary
has a missing shared library. `test-supertonic-flatpak.sh` synthesizes through
`SupertonicEngine`, runs a deterministic 200-character phrase at four
denoising steps, and checks the cancellation and missing-model error paths.

After installing or rebuilding the extension, **restart OBS** (or use
*File → Exit* and start it again): a running instance keeps the extension it
loaded at startup.

## Uninstall

```bash
flatpak uninstall --user com.obsproject.Studio.Plugin.YtChatTts
```

Settings and the saved API key are not removed by `flatpak uninstall`. Delete
them explicitly if you want a clean slate:

```bash
rm -rf ~/.var/app/com.obsproject.Studio/config/obs-studio/plugin_config/yt-chat-tts
```

## Google Cloud API key setup

The official provider needs a YouTube Data API v3 key. The no-key provider
does not; you can skip this section entirely if you only use *No-key
(experimental)*.

1. Go to <https://console.cloud.google.com/> and create or select a project.
2. Open **APIs & Services → Library**, search for **YouTube Data API v3** and
   click **Enable**.
3. Open **APIs & Services → Credentials → Create credentials → API key** and
   copy the key.
4. Edit the new key and restrict it:
   - **API restrictions → Restrict key → YouTube Data API v3**. This is the
     important restriction: the key must not be usable against unrelated
     Google APIs.
   - **Application restrictions**: leave this as **None**. Do **not** add an
     HTTP-referrer restriction. The live-chat connection uses gRPC, which does
     not send an HTTP `Referer` header, so a referrer-restricted key fails the
     `StreamList` call with a permission error even though the REST calls
     succeed. IP restrictions also do not help on a machine with a changing
     address.
5. Save. New keys and API enablement can take a few minutes to become active.
6. Paste the key into the dock's **API key** field and connect. The field is
   password-echoed, and the saved key is reloaded into that hidden field on the
   next start.

The key is used only by the bundled helper and only against Google's YouTube
Data API/gRPC endpoints. It travels on the helper's stdin, not on its command
line, and it is redacted from helper errors and the dock log.

## Using the dock

Open **Docks → YouTube Chat TTS**.

1. **Add the audio source first.** Under **Audio source**, click **Add TTS
   source to current scene** or **Add to all scenes**. The status line changes
   from `not added (audio will be silent)` to `added`. You can also add a
   source manually with **Sources → + → YouTube Chat TTS**; the plugin does not
   create a second copy if one already exists.
2. Set **Engine**, **Voice**, **Rate**, **Pitch**, **Amplitude** and **Volume**.
   For the local server, also set **Server URL**, **Server voice** and **Server
   model**; see [Local server](#local-server-kokoro--pocket-tts).
   **Test Voice** speaks a test sentence through the audio source, so use it to
   check levels and recording before going live. With no source added, Test
   Voice is buffered but silent; the dock warns about that.
3. Paste a YouTube video URL or 11-character video ID, or a channel URL/handle
   that currently has a live stream, into **Video**.
4. Choose a **Provider** (see below) and, for the official provider, an API
   key.
5. Click **Connect**. Messages start being spoken as they arrive. The status
   line shows `Connected (official)` or `Connected (innertube)`; the log shows
   the fallback warning when Auto changes provider.
6. Optionally enable **Connect automatically** to reconnect on the next OBS
   start when a URL/ID is already saved. Auto-connect failures go to the dock
   log, not a modal dialog.

`Disconnect` stops reading and terminates the helper. **Skip Current** drops
queued messages (the sentence already playing may finish); **Clear Queue**
empties the queue. **Clear Log** clears the dock's log pane only; the OBS log
file is unaffected.

### Speech engine, voice and speed

The **Engine** selector has two entries:

| Engine | Voice | Notes |
| --- | --- | --- |
| `Supertonic — natural (CPU)` (default) | Ten built-in styles: `F1`–`F5`, `M1`–`M5` | Bundled Supertonic 3 model, ~380 MB, 44,100 Hz output resampled to 22,050 Hz. Speed (0.90–1.50×) and denoising steps (4–16; 8 is the default) are adjustable. |
| `Local server — Kokoro / Pocket (experimental)` | Whatever the server offers | Talks to a local OpenAI-compatible `/v1/audio/speech` endpoint. The model runs outside the plugin. |

Supertonic is the only bundled engine. If its model or ONNX Runtime is
missing or corrupt, the plugin logs the exact reason and stops; there is no
espeak-ng fallback any more. Reinstall the extension or fix the model path,
then click **Test Voice** or reconnect.

### Supertonic 3 performance

Measured on the reference laptop (i5-13450HX, 16 threads, 4 ONNX Runtime
intra-op threads, 4 or 8 denoising steps, `F1` voice):

| measurement | 4 steps | 8 steps |
|---|---:|---:|
| First synthesis (includes model load) | 0.82–0.94 s | 1.01–1.06 s |
| Warm 2–3 word message | 0.26–0.33 s | 0.42–0.58 s |
| 200-character message | 1.92 s | 3.30 s |
| Peak RSS | 656 MB | 657 MB |

A warm short message carries ~0.4 s of leading and ~0.6 s of trailing
silence. Number/currency symbols and CJK text are unreliable without a
language/number normalizer. Full numbers, the ASR spot-check and the blind
listening page are in
[`docs/supertonic-evaluation-2026-11-03.md`](docs/supertonic-evaluation-2026-11-03.md).

### Local server (Kokoro / Pocket TTS)

The local engine is the quality path. Kokoro and Pocket TTS sound considerably
better than the bundled Supertonic voice, but their weights are large and their
licences differ, so the plugin does not ship them. Run one on the same machine:

- **Kokoro** (Apache-2.0):
  `docker run -p 8880:8880 ghcr.io/remsky/kokoro-fastapi-cpu`, then set
  **Server URL** to `http://127.0.0.1:8880/v1/audio/speech`, **Server voice**
  to `af_heart`, and **Server model** to `kokoro`.
- **Pocket TTS** (CC-BY-4.0 weights, gated on Hugging Face): run
  `pocket-tts serve` or `PocketTTS.cpp --server` and point **Server URL** at the
  server's `/v1/audio/speech` endpoint. The first request may download weights.

The server returns 24 kHz audio; the plugin decodes the WAV and resamples it to
the 22,050 Hz OBS source with a windowed-sinc filter. If the server is not
running, the dock logs the connection error per message and keeps the queue.
Start the server and the next message works without restarting OBS.

Pocket is deliberately not bundled: its sherpa/ONNX bundle is a January 2026
checkpoint, and one measured utterance hung inside the model for over two
minutes without ever invoking its progress callback. A separate process can be
killed; the plugin cannot cancel that safely in-process. Keeping it behind a
local server also keeps the model licence and the ~100 MB of weights out of the
Flatpak.

How the controls apply:

- **Speed** is Supertonic's playback multiplier. The UI range is 0.90–1.50×;
  0.90× is the natural conversational pace and 1.50× is the fastest
  recommended. Values below 0.90× spend the extra duration on long pauses
  rather than slower words, so they are not exposed.
- **Denoising steps** controls quality versus time: 8 is the default, 4 is
  fastest, 16 is slowest and often cleanest.
- **Low-latency streaming** applies to Supertonic. The engine emits one chunk
  per sentence, so multi-sentence messages start speaking sooner; a
  one-sentence message is unchanged. The audio is identical and still
  DC-blocked and faded.
- **Volume** applies to every engine and is owned by the audio buffer.
- Changing **Engine** or a Supertonic parameter restarts the speech worker so
  the next utterance uses the new selection; the chat connection is not
  dropped.

If the bundled Supertonic model or ONNX Runtime is missing or corrupt, the
dock logs the exact reason and the worker stops. There is no espeak-ng
fallback any more: reinstall the extension or fix the model path, then click
**Test Voice** or reconnect. `./scripts/smoke-flatpak.sh` fails on a package
that does not link the bundled ONNX Runtime.

### Audio source and recording

`YouTube Chat TTS` is a normal OBS audio input source, so it appears in the
Audio Mixer and can be routed, filtered and monitored like any other source.
Every synthesized utterance is DC-blocked and faded in/out over 5 ms before it
reaches the source, so a clip that stops mid-waveform does not click when the
buffer falls silent. To record it:

1. Confirm the source is in the active scene and not muted in the Audio Mixer.
2. Set its volume there (the dock's **Volume** control scales the same signal).
3. In **Settings → Output → Recording**, make sure at least one recorded audio
   track includes this source. OBS's default track 1 normally does.
4. Record a short test and confirm the speech is present in the recording.

Expected behavior (pending live acceptance; see item 11 in
[`docs/acceptance-checklist.md`](docs/acceptance-checklist.md)): scene switches
should not stop chat reading, and with **Add to all scenes** the source exists
in every scene. Removing the source is intended to stop audible speech while
chat continues to be synthesized; re-adding it should restore audio for
subsequent messages. Removal and re-add are expected not to crash OBS and not
to replay speech buffered before the removal.

## Provider modes

| Mode | API key | Behaviour |
| --- | --- | --- |
| **Auto** (default) | optional | With a key, tries the official provider first. On a **pre-connection** `auth`, `quota` or `network` failure it logs `official provider unavailable (...); falling back to experimental innertube` and continues with InnerTube. Without a key it starts directly with InnerTube. |
| **Official API** | required | YouTube Data API v3 resolves the video/channel, then the gRPC `V3DataLiveChatMessageService.StreamList` call streams messages. |
| **No-key (experimental)** | never used | Reads public watch pages and YouTube's live-chat endpoint without a Google Cloud project. |

Fallback is deliberately conservative. Auto does **not** switch providers
after the official stream has connected, because that would duplicate chat
history instead of recovering; it also does not fall back on a definitive
stream state (`not_live`, `chat_disabled`) or a `parse` failure, because the
other provider would usually report the same thing. The dock always shows the
provider actually in use.

The official provider is the supported path. It cannot guarantee access to
chats for streams you do not own; that case is explicitly recorded as
unverified in [`docs/acceptance-checklist.md`](docs/acceptance-checklist.md).
If YouTube rejects a public chat for your key, use Auto/No-key or an
owned/OAuth-authorized setup.

## Quota, reconnects and persistence

- The YouTube Data API v3 has a finite daily quota (10,000 units by default per
  Google Cloud project). A direct video ID uses `videos.list` (1 unit); a
  channel or handle also uses `channels.list` (1 unit) and `search.list`
  (100 units). Connecting is a one-time resolution, not polling.
- Live chat is consumed through one long-lived stream, so message volume does
  not consume quota per message.
- A `quota` error is treated as fatal for that connection: there is no retry
  storm. Wait for the quota to reset (midnight Pacific) or switch to
  **No-key (experimental)**.
- Network failures before connecting are retried a bounded number of times;
  after connecting, reconnects use capped exponential backoff. The status line
  shows `Reconnecting…` while this happens.
- The extension stops reading on `ended` and reports `Stream ended`; it does
  not loop.
- Settings are saved 500 ms after the last change to
  `~/.var/app/com.obsproject.Studio/config/obs-studio/plugin_config/yt-chat-tts/settings.json`
  with mode `0600`. Voice, rate, pitch, amplitude and volume apply live without
  reconnecting; the queue is preserved.
- To reset settings, disconnect, delete that file and restart OBS.

## Privacy and YouTube Terms of Service

- The **official** provider sends your API key and the requested video ID only
  to Google's YouTube Data API/gRPC endpoints. That is the supported mode.
- The **no-key InnerTube** provider is **unofficial and experimental**. It
  reads public YouTube watch pages and live-chat responses without a developer
  key and may stop working whenever YouTube changes those responses. It may
  also conflict with the YouTube Terms of Service
  (<https://www.youtube.com/t/terms>) or the API Services Terms of Service;
  you enable and use it at your own risk. It is never used when the provider is
  set to *Official API*.
- Chat messages are processed locally and spoken locally. This plugin does not
  send messages, moderate users, or authenticate as your YouTube account, and
  it does not upload chat content anywhere.
- Supertonic and ONNX Runtime run offline inside the Flatpak sandbox. They
  make no network calls and add no telemetry.
- API keys are stored in the owner-only settings file, are never written to the
  dock log or OBS log, and are never passed on a command line.
- Only public live chat that the provider can read is accessed. Do not use the
  plugin to record or republish chat in violation of YouTube's terms or the
  rights of participants.

## Troubleshooting

Check **Help → Log Files → View Current Log** for `[yt-chat-tts]` lines. The
dock's **Log** pane mirrors the important status and error lines.

| Symptom | Cause and fix |
| --- | --- |
| Dock is missing after install | Restart OBS. Confirm the extension is installed with `flatpak list --app --runtime \| grep YtChatTts` and that `./scripts/smoke-flatpak.sh` passes. |
| Dock says `not added (audio will be silent)` | Add the `YouTube Chat TTS` source (current scene or all scenes). Chat still connects without it. |
| Test Voice is silent | Source missing, muted, or at zero volume; check the Audio Mixer and the dock's **Volume**. |
| `API key rejected; check the key and API restrictions` | Key is wrong, YouTube Data API v3 is not enabled, or the key is restricted to the wrong API or to HTTP referrers. Remove the referrer restriction and allow YouTube Data API v3. |
| `YouTube quota exceeded; try No-key or wait until midnight PT` | Daily quota exhausted. Use No-key or wait for the Pacific-time reset. |
| `No active live stream found` | URL is past, upcoming, or not a live video. Start from an active live URL. With the official provider a private video is rejected as an API/auth error (`API key rejected…`) rather than `not_live`; with the no-key provider it is reported as not live. |
| `Live chat is disabled for this stream` | The broadcast has chat turned off; nothing can read it. |
| `YouTube response changed; try the other provider` | The experimental provider's parsing no longer matches YouTube. Switch to the official provider (or update the plugin). |
| `Network error; retrying` | Transient DNS/TLS/transport problem; wait for the capped backoff. If it persists, check connectivity to `youtube.googleapis.com` and the gRPC endpoint. |
| Helper not found / chat never connects | Rebuild with `./scripts/build-helper.sh && ./scripts/build-flatpak.sh`; the Flatpak build now refuses to proceed without the helper. |
| Audio is fast, choppy or overlaps | A duplicate `YouTube Chat TTS` source is draining the shared buffer. The plugin keeps only one active consumer and logs the others as silent; delete unused duplicates from **Sources**. |
| Speech is too slow or too fast | Set **Speed** to `0.90`–`1.00x` for natural chat. `0.50x` is no longer exposed because it adds long pauses rather than slowing the words proportionally. |
| Local server engine logs `not reachable` | The Kokoro/Pocket server is not running or the URL/port is wrong. Start it, check the URL ends at `/v1/audio/speech`, and press **Test Voice**. The plugin retries on the next message; OBS does not need a restart. |
| Local server returns HTTP 4xx/5xx | The server rejects the voice/model names. Check **Server voice** and **Server model** against the server's documentation (`af_heart`/`kokoro` for Kokoro-FastAPI). |
| Dock logs `Supertonic is unavailable` or a model-file error | The extension is incomplete or a custom `supertonicModelPath` is wrong. Reinstall with `./scripts/build-flatpak.sh`, run `./scripts/smoke-flatpak.sh`, and correct the path. |
| Supertonic is slow on old hardware | It is CPU-only and the model is ~380 MB. Enable **Low-latency streaming**, lower **Denoising steps** to 4, shorten **Max length**, or reduce chat volume. |
| Rebuild fails after an OBS update | OBS/runtime ABI changes require rebuilding the extension. Run the clean rebuild commands above. |
| Build fails with a mount or busy error | A previous Flatpak build mount is stale. `flatpak kill com.obsproject.Studio`, remove any leftover `build-flatpak` directory, and rerun `scripts/build-flatpak.sh`. Do not delete `~/.var/app/com.obsproject.Studio`. |

## Development and tests

Host tests need CMake 3.28+, Ninja, Qt 6 development packages (Core, Widgets,
Network, Test), OBS development headers exporting the `OBS::libobs` and
`OBS::obs-frontend-api` CMake targets, and Go 1.22+. Point CMake at those
prefixes if they are not in the default search path:

```bash
export CMAKE_PREFIX_PATH=/path/to/obs:/path/to/qt6
export LD_LIBRARY_PATH=/path/to/qt6/lib:/path/to/obs/lib
CXXFLAGS="-I/path/to/obs/include" ./scripts/test-host.sh
```

`./scripts/test-host.sh` builds and runs the C++ unit tests with CTest (chat
client/JSONL parsing, settings and file mode, text normalization, message
filtering, bounded queue, WAV parsing, Supertonic text/engine policy,
settings, audio buffer and TTS worker)
and then runs `go test ./...` and `go vet ./...` for the helper.

Flatpak-side checks:

```bash
./scripts/smoke-flatpak.sh            # layout, helper, Supertonic/ONNX, licenses, ldd
./scripts/test-supertonic-flatpak.sh  # real synthesis, 200-char benchmark, error paths
```

Manual sandbox inspection:

```bash
flatpak run --command=sh com.obsproject.Studio -c '
  ldd /app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so | grep "not found" && exit 1 || true
  test -x /app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper
  /app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper --version
'
```

The full interactive acceptance matrix (live providers, recording, settings
mode, process cleanup) and its current status are recorded in
[`docs/acceptance-checklist.md`](docs/acceptance-checklist.md).

### Helper development

```bash
cd helper && go test ./... && go vet ./...
./scripts/gen-proto.sh    # only after editing proto/stream_list.proto
```

`gen-proto.sh` needs `protoc` 27.2 and the pinned `protoc-gen-go` /
`protoc-gen-go-grpc` versions. Generated bindings are committed, so ordinary
builds do not need `protoc`.

## Installed layout

Inside the extension prefix `/app/plugins/YtChatTts`:

```text
lib/obs-plugins/yt-chat-tts.so                              OBS module
lib/libonnxruntime-ytc.so.1                                 bundled ONNX Runtime (CPU)
share/obs/obs-plugins/yt-chat-tts/locale/en-US.ini          dock strings
share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper         bundled chat helper
share/obs/obs-plugins/yt-chat-tts/bin/supertonic-probe      packaging/latency probe
share/obs/obs-plugins/yt-chat-tts/supertonic/onnx/          Supertonic 3 ONNX models
share/obs/obs-plugins/yt-chat-tts/supertonic/voice_styles/  ten voice styles
share/obs/obs-plugins/yt-chat-tts/supertonic/LICENSE        model license
share/metainfo/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSE
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/THIRD_PARTY_NOTICES.md
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/onnxruntime/
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/supertonic/
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/supertonic-3/
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/go/
```

ONNX Runtime headers and the `patchelf` helper are build-only and pruned from
the installed extension. The plugin links `libonnxruntime-ytc.so.1` directly.

The `supertonic-probe` binary is a packaged diagnostic/test tool used by
`scripts/test-supertonic-flatpak.sh`. It synthesizes through the shipped
`SupertonicEngine`, prints sample rate/sample counts and timings, exercises
cancellation, and reports peak RSS.

## Limitations

- Supertonic is CPU-only and adds about 381 MB of model data plus ONNX
  Runtime to the extension (roughly 440–470 MB installed). Synthesis is
  faster than real time on a typical desktop CPU after the one-time model
  load, but it is heavier per character than the removed espeak-ng fallback.
- Only the ten built-in Supertonic voices are supported. Custom voice styles
  and downloadable voices are out of scope.
- Number/currency symbols and CJK text are unreliable without a language or
  number normalizer; the engine is currently pinned to the `en` language tag.
- There is no automatic fallback engine. A missing or corrupt model stops the
  worker with a readable error instead of speaking through espeak-ng.
- Only public live chat readable by the selected provider is supported. The
  official `streamList` path may reject chats for videos not owned by the key;
  that case is not claimed as verified.
- The InnerTube provider is unofficial and may break without notice. Live-chat
  replay is not supported: past VODs and upcoming premieres are rejected as
  `not_live`.
- **UI responsiveness:** the helper is started and stopped synchronously on the
  OBS UI thread, and changing voice/rate/pitch/amplitude restarts the speech
  worker. Connecting can block briefly while the helper starts; disconnecting
  or unloading can wait for process termination. A hung helper can delay OBS
  for up to a few seconds (5 s start wait plus up to 2 s terminate and 2 s kill
  grace). This is known inherited debt, not fixed in v0.1.0.
- The extension is x86-64 only and requires a rebuild after OBS or runtime
  updates that change the plugin ABI.
- No moderation, message sending, OAuth sign-in or historical chat download.
- Not published on Flathub; install it from this repository.

## Version

**0.3.0** (2026-11-03). Supertonic 3 is now the bundled engine; Piper and
espeak-ng were removed. The local OpenAI-compatible server engine remains.
A missing or corrupt Supertonic model now reports a clear error instead of
falling back to espeak-ng.

## Development disclosure

This project was developed with AI coding assistants and reviewed, tested and
packaged by the maintainer. AI tools assisted with implementation, tests,
documentation and audio-quality review. Every change was compiled and exercised
through host tests, the Flatpak build/smoke tests, the Supertonic probe and OBS.

## License

Project code is GPL-2.0-or-later; see [`LICENSE`](LICENSE). Third-party
components and their licenses are documented in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). Authoritative texts for
ONNX Runtime, the Supertonic model and inference code, and every statically
linked Go dependency are vendored under [`LICENSES/`](LICENSES/).
