# Submitting YouTube Chat TTS to the OBS Plugins directory

The OBS Project lists third-party plugins through the **Resources → Plugins**
section of the OBS Forums:

- Plugin directory: <https://obsproject.com/forum/plugins/>
- Submission policy: <https://obsproject.com/forum/threads/forum-resource-and-ip-policy.178569/>
- Add Resource button: visible after enabling two-factor authentication and
  waiting at least 24 hours after account creation.

This document is a project-specific checklist, not a replacement for the
official policy. Read the policy before submitting.

## Before submitting

- [ ] **GPL-compatible source is public and current.** OBS links plugins under
      GPL-2.0-or-later; this repository is GPL-2.0-or-later and the source is on
      GitHub.
- [ ] **Stable release.** Publish a release with a downloadable Flatpak bundle
      and SHA-256, not a debug build or a branch archive. The current release is
      `v0.3.0`.
- [ ] **Reputable download host.** Use the GitHub release assets:
      `OBS-Youtube-Chat-TTS-0.3.0.flatpak` and
      `OBS-Youtube-Chat-TTS-0.3.0.flatpak.sha256`.
- [ ] **English description.** The resource description must be in English (or
      include an English translation).
- [ ] **Issue tracker link.** Point users at
      <https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/issues>.
- [ ] **Screenshots.** Include at least one screenshot of the dock and one of
      the OBS Audio Mixer/TTS source. Do not use the OBS logo as the plugin
      icon.
- [ ] **AI disclosure.** This project was developed with AI coding assistance.
      The policy requires a disclaimer describing what AI tools were used and
      how. Failure to disclose can cause rejection; even with disclosure, the
      OBS team may deny heavily AI-assisted resources. Do not omit this.
- [ ] **Third-party notices.** ONNX Runtime and the Supertonic model/code
      licenses are documented in `THIRD_PARTY_NOTICES.md` and installed with
      the extension.

## Branding warning

The OBS policy says not to use the letters “OBS” to identify a resource, and
to avoid suggesting official affiliation. The product title used here is
**YouTube Chat TTS**; that is the name to submit.

The GitHub repository is currently named `OBS-Youtube-linux-plugin`. The policy
allows `obs-` in repository names only in limited cases and discourages it for
new projects. Before submitting, rename the repository to something like
`youtube-chat-tts` or `youtube-chat-tts-flatpak`, or make the third-party
status unmistakable in the README. Do not use the OBS logo in the icon or
screenshots beyond an unmodified application screenshot that clearly shows a
third-party plugin.

## Suggested resource fields

**Title**

```text
YouTube Chat TTS
```

**Category**

```text
Plugins
```

**Tags**

```text
youtube, live-chat, text-to-speech, tts, accessibility, flatpak, linux
```

**Version**

```text
0.3.0
```

**Download**

```text
https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/releases/download/v0.3.0/OBS-Youtube-Chat-TTS-0.3.0.flatpak
```

**SHA-256**

```text
8134d0c5ba990f5edb7f649dcafc3073e4f743c52ffcfdf79166aabee15b68b6
```

## Ready-to-paste resource description

```markdown
YouTube Chat TTS reads YouTube live chat aloud inside OBS Studio using the
bundled, offline Supertonic 3 neural voice and mixes the speech into OBS as a
native audio source. It is a third-party Flatpak extension and is not
affiliated with or endorsed by the OBS Project.

## Features

- Offline Supertonic 3 speech with ten bundled voices (F1-F5, M1-M5).
- Adjustable speed (0.90-1.50x) and denoising steps (4-16).
- Official YouTube Data API v3 provider and experimental keyless InnerTube
  provider, with automatic fallback.
- Message filtering: max length, per-user/global cooldowns, command prefix,
  URL stripping, blocklist, Super Chats, memberships and moderators.
- Bounded queue with Skip Current and Clear Queue.
- Native OBS audio source, so speech is recorded and streamed like any other
  audio source.
- Optional local OpenAI-compatible TTS server engine (Kokoro/Pocket TTS).

## Requirements

- Linux x86-64.
- OBS Studio from Flathub (the extension targets the Flatpak build).
- Flatpak 1.14 or newer.
- About 450 MB of disk space for the extension and Supertonic model.

## Install

Download `OBS-Youtube-Chat-TTS-0.3.0.flatpak` from the release assets:

```bash
flatpak install --user --bundle ./OBS-Youtube-Chat-TTS-0.3.0.flatpak
```

Then restart OBS and open **Docks → YouTube Chat TTS**. Add the
**YouTube Chat TTS** audio source before going live.

SHA-256 for the bundle:
`8134d0c5ba990f5edb7f649dcafc3073e4f743c52ffcfdf79166aabee15b68b6`

## Usage

1. Install the bundle and restart OBS.
2. Open **Docks → YouTube Chat TTS**.
3. Enter a YouTube live URL or video ID and connect.
4. Add the **YouTube Chat TTS** audio source to the current scene or all
   scenes.
5. Select a Supertonic voice and click **Test Voice**.

The official provider needs a YouTube Data API v3 key; the experimental
InnerTube provider works without a key but may break without notice.

## Troubleshooting

- If the dock is missing, restart OBS and confirm the extension is installed
  with `flatpak list --runtime | grep YtChatTts`.
- If speech is silent, add the audio source and check its mixer volume.
- If the dock reports a missing Supertonic model, reinstall the bundle.
- Report bugs at:
  https://github.com/vipulgote1999/OBS-Youtube-linux-plugin/issues

## Source and license

Source: https://github.com/vipulgote1999/OBS-Youtube-linux-plugin

Project code is GPL-2.0-or-later. ONNX Runtime, Supertonic and bundled Go
dependency notices are installed with the extension and documented in
`THIRD_PARTY_NOTICES.md`.

## AI development disclosure

This project was developed with AI coding assistants and reviewed, tested and
packaged by the maintainer. AI tools assisted with implementation, tests,
documentation drafts and audio-quality review. Every change was compiled and
exercised through host tests, Flatpak build/smoke tests, the Supertonic probe
and OBS; the maintainer is responsible for the released code and support.
```

## Review process

- While pending, the resource appears as **Deleted** in the resource list.
- Review time varies from about an hour to several months; it is done by
  volunteers.
- If a submission is rejected without follow-up, re-read the policy before
  resubmitting.
- The OBS team approves at its sole discretion. The AI-assistance disclosure is
  mandatory and may still lead to denial.

## Release checklist for this repository

```bash
./scripts/build-helper.sh
./scripts/build-flatpak.sh
./scripts/smoke-flatpak.sh
./scripts/test-supertonic-flatpak.sh
flatpak build-bundle ~/.local/share/flatpak/repo \
  dist/OBS-Youtube-Chat-TTS-0.3.0.flatpak \
  com.obsproject.Studio.Plugin.YtChatTts stable --runtime
sha256sum dist/OBS-Youtube-Chat-TTS-0.3.0.flatpak
```

Upload the `.flatpak` and `.flatpak.sha256` to the GitHub release, then create
the OBS Resources entry with the description above.
