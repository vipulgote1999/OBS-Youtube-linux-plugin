# OBS YouTube Chat TTS

A Flatpak OBS Studio extension that reads YouTube live chat aloud through the
offline espeak-ng speech engine and mixes the synthesized speech into OBS as a
native audio source. No cloud TTS, no Python inside OBS, no virtual audio
cables.

This is the Task 1 skeleton of the plugin: a C++17/Qt6 OBS module is built
inside the `org.freedesktop.Sdk//25.08` Flatpak sandbox, installs as the
Flatpak extension `com.obsproject.Studio.Plugin.YtChatTts`, and registers the
"YouTube Chat TTS" dock. Later tasks add the chat connection UI, the bundled
espeak-ng engine, voice filtering and queueing, and the audio source.

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
```

Then start `flatpak run com.obsproject.Studio`. Under **Docks** you will find
"YouTube Chat TTS". Opening it shows the placeholder status. The OBS log
(`Help → Log Files`) contains `[yt-chat-tts] loaded`.

The smoke test only checks what the build guarantees: the plugin binary and
its locale file landed at the expected paths inside the OBS Flatpak sandbox:

- `/app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so`
- `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/locale/en-US.ini`

## Layout

```text
CMakeLists.txt          plugin build (OBS::libobs / OBS::obs-frontend-api / Qt6)
src/plugin-main.cpp     module entry points, dock registration/removal
src/yt-chat-tts-dock.*  dock widget (placeholder)
src/version.h           plugin id, dock id, version constants
data/locale/en-US.ini   module locale strings
flatpak/                Flatpak manifest and AppStream metainfo
scripts/                build and smoke-test scripts
```

## License

Project code: GPL-2.0-or-later (see `LICENSE`).
Later tasks bundle espeak-ng, which is a separate executables licensed
GPL-3.0-or-later; its license text and source link will ship with it.
