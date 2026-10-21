# OBS YouTube Chat TTS

A Flatpak extension for OBS Studio that reads YouTube live chat aloud with the
bundled offline [espeak-ng](https://github.com/espeak-ng/espeak-ng) engine and
mixes the synthesized speech into OBS as a native audio source. There is no
cloud TTS, no virtual audio cable and no Python inside OBS.

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
- **Offline speech** through a statically linked espeak-ng 1.51 executable;
  the audio is fed to a native OBS input source named `YouTube Chat TTS`.
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
- OBS Studio from Flathub (tested with **32.2.2**):
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

## Build, install and verify

```bash
./scripts/build-helper.sh      # produces dist/ytchat-helper (static x86-64)
./scripts/build-flatpak.sh     # reruns build-helper.sh, then installs the extension
./scripts/smoke-flatpak.sh     # checks the installed extension inside the sandbox
./scripts/test-espeak-flatpak.sh
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
./scripts/test-espeak-flatpak.sh
./scripts/test-host.sh
```

`smoke-flatpak.sh` verifies, inside the OBS sandbox, that the module and
locale, the executable helper (`ytchat-helper --version`), the espeak-ng
binary and data, the AppStream metainfo and the license/notice files are all
present, and that neither the module nor espeak-ng has a missing shared
library. `test-espeak-flatpak.sh` additionally synthesizes speech to a WAV and
checks its RIFF header.

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
2. Set **Voice**, **Rate**, **Pitch**, **Amplitude** and **Volume**. **Test
   Voice** speaks a test sentence through the audio source, so use it to check
   levels and recording before going live. With no source added, Test Voice is
   buffered but silent; the dock warns about that.
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

### Audio source and recording

`YouTube Chat TTS` is a normal OBS audio input source, so it appears in the
Audio Mixer and can be routed, filtered and monitored like any other source.
To record it:

1. Confirm the source is in the active scene and not muted in the Audio Mixer.
2. Set its volume there (the dock's **Volume** control scales the same signal).
3. In **Settings → Output → Recording**, make sure at least one recorded audio
   track includes this source. OBS's default track 1 normally does.
4. Record a short test and confirm the speech is present in the recording.

Scene switches do not stop chat reading: with **Add to all scenes**, the source
exists in every scene. Removing the source stops audible speech while chat
continues to be synthesized; re-adding it restores audio for subsequent
messages. Removing and re-adding the source never crashes OBS and does not
replay speech buffered before the removal.

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
- The speech engine (espeak-ng) runs offline inside the Flatpak sandbox.
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
| `No active live stream found` | URL is past, upcoming, private, or not a live video. Start from an active live URL. |
| `Live chat is disabled for this stream` | The broadcast has chat turned off; nothing can read it. |
| `YouTube response changed; try the other provider` | The experimental provider's parsing no longer matches YouTube. Switch to the official provider (or update the plugin). |
| `Network error; retrying` | Transient DNS/TLS/transport problem; wait for the capped backoff. If it persists, check connectivity to `youtube.googleapis.com` and the gRPC endpoint. |
| Helper not found / chat never connects | Rebuild with `./scripts/build-helper.sh && ./scripts/build-flatpak.sh`; the Flatpak build now refuses to proceed without the helper. |
| Audio stutters or lags behind chat | Lower the message rate/size filters, shorten **Max length**, or raise the **Queue limit** cautiously. espeak-ng synthesis is CPU-bound. |
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
filtering, bounded queue, WAV parsing, espeak engine, audio buffer, TTS worker)
and then runs `go test ./...` and `go vet ./...` for the helper.

Flatpak-side checks:

```bash
./scripts/smoke-flatpak.sh          # installed layout, helper, espeak, licenses, ldd
./scripts/test-espeak-flatpak.sh    # espeak-ng runs inside the sandbox
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
share/obs/obs-plugins/yt-chat-tts/locale/en-US.ini          dock strings
share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper         bundled chat helper
share/obs/obs-plugins/yt-chat-tts/bin/espeak-ng             bundled speech engine
share/obs/obs-plugins/yt-chat-tts/espeak-ng-data/           voices and dictionaries
share/metainfo/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSE
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/THIRD_PARTY_NOTICES.md
share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/espeak-ng.txt
```

The espeak-ng executable is statically linked against its own library and has
no `libespeak-ng.so` dependency. It is invoked as a separate process with
`ESPEAK_DATA_PATH` pointing at the bundled data directory. Build-only espeak-ng
headers, static libraries, pkg-config files and vim/man documentation are
removed by the manifest and are not shipped.

## Limitations

- espeak-ng is a formant synthesizer and sounds robotic; it was chosen for v1
  to avoid a cloud dependency. The `TtsEngine` interface allows a future
  engine (for example Piper) without changing the chat pipeline.
- The bundled voice set is English (US) plus the other English voices shipped
  by espeak-ng (`en-us`, `en-gb`, `en-in`, `en-gb-scotland`, `en-029`, and any
  custom espeak-ng voice name you type).
- Only public live chat readable by the selected provider is supported. The
  official `streamList` path may reject chats for videos not owned by the key;
  that case is not claimed as verified.
- The InnerTube provider is unofficial and may break without notice.
- The extension is x86-64 only and requires a rebuild after OBS or runtime
  updates that change the plugin ABI.
- No moderation, message sending, OAuth sign-in or historical chat download.
- Not published on Flathub; install it from this repository.

## Version

**0.1.0** (2026-10-21). This is the first release: Flatpak extension, dock,
official and experimental chat providers, espeak-ng audio source, filtering,
queue and persisted settings. The `v0.1.0` release tag is pending completion of
the live acceptance matrix; see `docs/acceptance-checklist.md`.

## License

Project code is GPL-2.0-or-later; see [`LICENSE`](LICENSE). Third-party
components and their licenses are documented in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md); the full espeak-ng
GPL-3.0-or-later text used by the bundled executable is in
[`LICENSES/espeak-ng.txt`](LICENSES/espeak-ng.txt).
