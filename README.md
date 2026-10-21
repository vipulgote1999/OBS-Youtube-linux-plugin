# OBS YouTube Chat TTS

A Flatpak OBS Studio extension that reads YouTube live chat aloud through the
offline espeak-ng speech engine and mixes the synthesized speech into OBS as a
native audio source. No cloud TTS, no Python inside OBS, no virtual audio
cables.

This is the Task 1 skeleton of the plugin: a C++17/Qt6 OBS module is built
inside the `org.freedesktop.Sdk//25.08` Flatpak sandbox, installs as the
Flatpak extension `com.obsproject.Studio.Plugin.YtChatTts`, and registers the
"YouTube Chat TTS" dock. The extension also bundles the offline espeak-ng
1.51 speech engine as a standalone executable. Later tasks add the chat
connection UI, voice filtering and queueing, and the audio source.

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
"YouTube Chat TTS". Opening it shows the placeholder status. The OBS log
(`Help → Log Files`) contains `[yt-chat-tts] loaded`.

The smoke test only checks what the build guarantees: the plugin binary and
its locale file landed at the expected paths inside the OBS Flatpak sandbox:

- `/app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so`
- `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/locale/en-US.ini`

The espeak-ng test runs the bundled engine inside the same sandbox: it
checks the binary and data paths, prints the engine version, synthesizes
"hello from espeak" to a WAV with the `en-us` voice, and verifies the WAV
header and size. It exits non-zero if any check fails.

## Layout

```text
CMakeLists.txt          plugin build (OBS::libobs / OBS::obs-frontend-api / Qt6)
src/plugin-main.cpp     module entry points, dock registration/removal
src/yt-chat-tts-dock.*  dock widget (placeholder)
src/version.h           plugin id, dock id, version constants
data/locale/en-US.ini   module locale strings
flatpak/                Flatpak manifest and AppStream metainfo
scripts/                build, smoke-test, and espeak-ng test scripts
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
