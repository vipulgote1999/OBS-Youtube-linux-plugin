# OBS YouTube Chat TTS

A Flatpak OBS Studio extension that reads YouTube live chat aloud through the
offline espeak-ng speech engine and mixes the synthesized speech into OBS as a
native audio source. No cloud TTS, no Python inside OBS, no virtual audio
cables.

This is a C++17/Qt6 OBS Studio module built inside the
`org.freedesktop.Sdk//25.08` Flatpak sandbox and installed as the Flatpak
extension `com.obsproject.Studio.Plugin.YtChatTts`. It registers the
"YouTube Chat TTS" dock, connects to YouTube live chat through a bundled Go
helper, filters and queues messages, and speaks them with the bundled offline
espeak-ng engine into a native OBS audio source.

## Requirements

- Flatpak 1.14 or newer
- OBS Studio (Flatpak) via Flathub: `flatpak install flathub com.obsproject.Studio`
- Build toolchain, both from Flathub:

```bash
flatpak install flathub org.freedesktop.Sdk//25.08
flatpak install flathub org.flatpak.Builder
```

Installing `com.obsproject.Studio` first also brings in the
`com.obsproject.Studio//stable` runtime the extension builds against.
The SDK is the only thing that must be installed manually; everything else
(README below documents it as well) lives in this repository.

## Build and install the extension

```bash
./scripts/build-flatpak.sh
```

The script checks that `org.freedesktop.Sdk//25.08` is installed, then runs
`flatpak-builder --user --install --force-clean build-flatpak
flatpak/com.obsproject.Studio.Plugin.YtChatTts.yml`. The extension installs
user-level next to your OBS Studio install.

## Verify

```bash
./scripts/smoke-flatpak.sh
# smoke: extension installed
./scripts/test-espeak-flatpak.sh
```

Then start `flatpak run com.obsproject.Studio`. Under **Docks** you will find
"YouTube Chat TTS". The OBS log (`Help → Log Files`) contains
`[yt-chat-tts] loaded`.

The smoke test only checks what the build guarantees: the plugin binary and
its locale file landed at the expected paths inside the OBS Flatpak sandbox:

- `/app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so`
- `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/locale/en-US.ini`

The espeak-ng test runs the bundled engine inside the same sandbox: it
checks the binary and data paths, prints the engine version, synthesizes
"hello from espeak" to a WAV with the `en-us` voice, and verifies the WAV
header and size. It exits non-zero if any check fails.

## Using the dock

Open **Docks → YouTube Chat TTS** and add the audio source before going live:

1. Under **Audio source**, click **Add TTS source to current scene** (or
   **Add to all scenes**). The status line below the buttons reports whether
   the source is present; it is refreshed automatically when scenes change.
2. Under **Connection**, paste a YouTube video URL or ID. An API key is
   optional: **Auto** uses the official YouTube API when a key is present and
   otherwise falls back to the experimental no-key provider; **Official API**
   requires a key; **No-key (experimental)** never uses one. **Get an API key**
   opens the Google Cloud credentials page.
3. Click **Connect**. Connect is refused with a warning when the URL/ID is
   empty. If no TTS audio source exists, the dock warns without blocking chat
   reading; speech is simply silent.
4. Enable **Connect automatically** to connect on the next OBS start when a
   URL/ID is saved. Auto-connect reports failures in the log instead of
   opening a dialog.

Settings are applied to the running pipeline as controls change and saved
automatically 500 ms after the last edit, so there is no separate Save button.
Voice, rate, pitch, amplitude and volume changes take effect without
reconnecting; the queue is preserved.

**Voice** offers espeak-ng voices (`en-us`, `en-gb`, `en-in`,
`en-gb-scotland`, `en-029`) and also accepts a custom voice name. Rate, pitch,
amplitude and volume have numeric sliders; **Read the author name** and the
`{author}`/`{message}` template control what is spoken.

**Filtering** limits message length, per-user and global cooldowns, queue
size, command handling, URL stripping, blocked phrases (one per line), Super
Chats, new members and moderators.

**Queue** lists the next five accepted messages as `author: text`, refreshing
while speech runs. **Skip Current** drops queued messages (the sentence already
playing may finish) and **Clear Queue** empties everything.

The **Log** pane keeps the last 500 lines and has a **Clear Log** button.
Helper failures are mapped to actionable text: `auth` (rejected key), `quota`
(exceeded quota), `not_live` (no active stream), `chat_disabled` (chat
disabled), `network` (retrying) and `parse` (provider response changed). The
connected/connecting status shows whether the `official` or `innertube`
provider is in use. The API key is never written to the log.

All user-facing dock strings live in `data/locale/en-US.ini` and are loaded
through `obs_module_text()`.

## Go chat helper

The YouTube chat connection helper is a standalone Go program under
`helper/`. Build the static Linux x86-64 binary into `dist/` (gitignored)
and run its tests with:

```bash
./scripts/build-helper.sh
cd helper && go test ./... && go vet ./...
```

Go 1.22 or newer is required. The helper reads one JSON config object on
stdin (`{"provider":"auto","video":"...","hl":"en"}`, plus `api_key` when
the official provider is used) and writes JSON Lines events on stdout; logs
go to stderr only, and the API key is never logged or included in error
text.

The official provider resolves the input with the YouTube Data API
(`channels.list`, `search.list`, `videos.list`), then streams messages from
YouTube's `V3DataLiveChatMessageService` gRPC endpoint. It emits
`connecting`, `connected`, `message`, `reconnecting`, and `ended` events.
Errors use the codes `auth`, `quota`, `not_live`, `chat_disabled`,
`network`, `parse`, and `ended`; `fatal` is set when retrying cannot help.

`auto` uses the official provider when an API key is present and falls back to
the experimental keyless InnerTube provider on a pre-connection failure;
without a key it starts directly with InnerTube. InnerTube is experimental
because YouTube can change its watch-page and chat response formats without
notice.

The protobuf definition and its generated Go bindings live under
`helper/proto/`. The generated files are committed, so building the helper
does not need `protoc`. After editing `stream_list.proto`, regenerate them
with `./scripts/gen-proto.sh`, which requires protoc 27.2 and the pinned
`protoc-gen-go`/`protoc-gen-go-grpc` versions listed in the script.

## Layout

```text
CMakeLists.txt          plugin build (OBS::libobs / OBS::obs-frontend-api / Qt6)
src/plugin-main.cpp     module entry points, dock registration/removal
src/yt-chat-tts-dock.*  dock widget: connection, voice, filtering, queue, audio, log
src/plugin-context.*    owns chat client, filter, queue, TTS worker and settings
src/version.h           plugin id, dock id, version constants
data/locale/en-US.ini   module locale strings
flatpak/                Flatpak manifest and AppStream metainfo
helper/                 Go chat helper: JSONL protocol, input resolver, official provider, proto bindings
scripts/                build, smoke-test, espeak-ng, proto-generation, and helper-build scripts
```

The bundled espeak-ng runtime lives next to the plugin files inside the
extension:

- `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/espeak-ng` —
  standalone engine executable, statically linked against the bundled
  espeak-ng library (it has no `libespeak-ng.so` dependency)
- `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/espeak-ng-data/` —
  voice and dictionary data; point `ESPEAK_DATA_PATH` at this directory when
  invoking the executable

## License

Project code: GPL-2.0-or-later (see `LICENSE`).
The bundled espeak-ng 1.51 is a separate executable licensed
GPL-3.0-or-later; its license text is in `LICENSES/espeak-ng.txt` and its
source URL and build details are recorded in `THIRD_PARTY_NOTICES.md`.
