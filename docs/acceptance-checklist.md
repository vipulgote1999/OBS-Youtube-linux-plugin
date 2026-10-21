# v0.1.0 Acceptance Checklist

Status legend: `[x]` passed and evidenced in this file, `[ ]` not run.

- **Version:** 0.1.0
- **Branch:** `feature/yt-chat-tts`
- **Headless gate commit:** see the Task 12 report (`docs` finalization commit)
- **Target:** OBS Studio Flatpak 32.2.2 on x86-64, extension
  `com.obsproject.Studio.Plugin.YtChatTts`
- **Date of headless run:** 2026-10-21

## 1. Clean-build gate — PASS

Run from a clean checkout with `build/`, `build-flatpak/` and `dist/` removed.
The Flatpak download cache (`.flatpak-builder/`) and the user's OBS
configuration were left alone.

Environment used:

```bash
export PATH=/tmp/opencode/host-deps/usr/bin:/tmp/opencode/toolchain/go/bin:$PATH
export LD_LIBRARY_PATH=/tmp/opencode/host-deps/usr/lib/x86_64-linux-gnu
export CMAKE_PREFIX_PATH=/tmp/opencode/host-deps/usr
export CXXFLAGS=-I/tmp/opencode/host-deps/usr/include
```

Commands and results:

| Check | Command | Result |
| --- | --- | --- |
| [x] Remove build output | `rm -rf build build-flatpak dist` | Clean; caches and OBS config untouched |
| [x] Build helper | `./scripts/build-helper.sh` | `dist/ytchat-helper: ELF 64-bit LSB executable, x86-64, statically linked, stripped` |
| [x] Build + install extension | `./scripts/build-flatpak.sh` | Exit 0; extension installed as `com.obsproject.Studio.Plugin.YtChatTts/x86_64/stable` |
| [x] Installed-layout smoke | `./scripts/smoke-flatpak.sh` | `smoke: extension installed (module, locale, helper, espeak-ng, metainfo, licenses)` |
| [x] espeak-ng in sandbox | `./scripts/test-espeak-flatpak.sh` | `eSpeak NG text-to-speech: 1.51` plus a valid `RIFF` WAV |
| [x] Host + Go tests | `./scripts/test-host.sh` | CTest 9/9 passed; `go test ./...` and `go vet ./...` exit 0 |

### Sandbox verification (brief Step 4)

```bash
flatpak run --command=sh com.obsproject.Studio -c '
  ldd /app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so | grep "not found" && exit 1 || true
  test -x /app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper
  /app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper --version || true
'
```

Result: `[x]` exit 0, no missing shared libraries, helper executable, prints
`ytchat-helper 0.1.0`.

### Installed-layout evidence

Every path below was verified by `scripts/smoke-flatpak.sh` inside the OBS
sandbox:

- [x] `/app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so`
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/locale/en-US.ini`
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper` (executable)
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/espeak-ng` (executable)
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/espeak-ng-data/` (488 files, ~13 MB)
- [x] `/app/plugins/YtChatTts/share/metainfo/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml`
- [x] `/app/plugins/YtChatTts/share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSE`
- [x] `/app/plugins/YtChatTts/share/licenses/com.obsproject.Studio.Plugin.YtChatTts/THIRD_PARTY_NOTICES.md`
- [x] `/app/plugins/YtChatTts/share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/espeak-ng.txt`
- [x] Build-only espeak-ng artifacts are absent (`include/espeak-ng`,
      `lib/libespeak-ng.a`, pkg-config, vim/man pages)
- [x] `ldd` reports no `not found` for the module or espeak-ng
- [x] AppStream compose succeeded during the Flatpak build

## 2. Live OBS / provider / recording matrix — PENDING

> **Not run.** The user's OBS session was running throughout Task 12. Per the
> task constraints, OBS was not launched, the session's configuration was not
> touched, the interactive E2E matrix was not executed, and the `v0.1.0` tag
> was not created. Every item in this section is **PENDING**, not passed.

Operator prerequisites:

- OBS Studio Flatpak 32.2.2 with the extension from this commit installed.
- Restart OBS after installing so it loads the new extension.
- A public, currently-live YouTube video URL; a second URL for a chat-disabled
  stream; and a past/upcoming/private URL for the `not_live` item.
- A valid YouTube Data API v3 key (official items) and a deliberately invalid
  key (fallback item). Paste keys into the dock; never put one on a command
  line or in a log.
- OBS log directory for CLI checks:
  `~/.var/app/com.obsproject.Studio/config/obs-studio/logs/`
- Latest log shortcut:
  `LOG=$(ls -t ~/.var/app/com.obsproject.Studio/config/obs-studio/logs/*.txt | head -1)`

Record the actual result and date next to each item when executed.

| # | Status | Check | How to run / expected result |
| --- | --- | --- | --- |
| 1 | PENDING | Extension loads, dock appears, source is registered | Start OBS. **Docks → YouTube Chat TTS** exists. In **Sources → +**, `YouTube Chat TTS` is listed. `grep '\[yt-chat-tts\]' "$LOG"` shows `loaded` and resolved helper/espeak paths. |
| 2 | PENDING | Official provider, valid key, public live chat | Set **Provider = Official API**, paste the valid key and a live URL, **Connect**. Status becomes `Connected (official)`; new chat messages are spoken; log has no key. |
| 3 | PENDING | Bad key with `Auto` warns and falls back to InnerTube | Set **Provider = Auto**, paste an invalid key and a live URL, **Connect**. Dock log shows `official provider unavailable (auth); falling back to experimental innertube`; status becomes `Connected (innertube)` and messages are spoken. |
| 4 | PENDING | No-key provider on a public live chat | Set **Provider = No-key (experimental)**, clear the key, connect to a live URL. Status `Connected (innertube)`; messages are spoken. |
| 5 | PENDING | Invalid / past / upcoming URL gives `not_live`, no loop | Connect with a past VOD URL, an upcoming premiere URL and a private URL (one at a time). Dock shows `No active live stream found`; status stops in error/ended; the log does not repeat connect attempts indefinitely. |
| 6 | PENDING | Chat-disabled stream gives `chat_disabled` | Connect to a live stream whose chat is disabled. Dock shows `Live chat is disabled for this stream`; no repeated retries. |
| 7 | PENDING | Stream ends: `ended`, no crash, no orphan helper | While connected to a live stream, let it end (or use a stream ending during the test). Dock shows `Stream ended`; OBS stays responsive. Then `pgrep -af 'ytchat-helper|espeak-ng'` shows no helper for that stream. |
| 8 | PENDING | High-volume chat stays bounded | Connect to a busy chat (or a replay with high rate) for at least 5 minutes. Dock queue count never exceeds **Queue limit**; log shows skipped/dropped messages; memory stays flat. `for i in $(seq 1 12); do ps -o rss= -p "$(pgrep -f ytchat-helper)"; sleep 5; done` |
| 9 | PENDING | Long / emoji-only / URL-only / blocklisted messages | Send or observe messages longer than **Max length**, emoji-only, URL-only, and one containing a blocklisted phrase. Long messages are truncated, emoji-only/URL-only/blocklisted are skipped as configured, normal messages still speak. |
| 10 | PENDING | OBS exit while connected leaves no processes | While connected and speaking, **File → Exit**. `pgrep -af 'ytchat-helper\|espeak-ng'` prints nothing; no crash dialog; `grep -i 'crash\|segfault' "$LOG"` is clean. |
| 11 | PENDING | Scene switch, source removal, source re-add | Switch between two scenes (source added to all scenes via the dock), then remove the `YouTube Chat TTS` source in the Audio Mixer and add it again with **Sources → +**. OBS does not crash; later messages are audible again; old speech is not replayed. |
| 12 | PENDING | Recording contains the TTS audio | Add the source, click **Test Voice**, record 10 s via **Start Recording**. Play the file and confirm speech is audible; optionally: `ffprobe -v error -show_entries stream=index,codec_type,channels -of compact <recording>` and `ffmpeg -i <recording> -af volumedetect -f null -` (mean volume must not be `-inf`). |
| 13 | PENDING | Settings file mode is `600` | After changing a setting: `stat -c '%a' ~/.var/app/com.obsproject.Studio/config/obs-studio/plugin_config/yt-chat-tts/settings.json` → `600`. |
| 14 | PENDING | Bad key on `Official API` shows `auth`, no retry storm | Set **Provider = Official API**, paste an invalid key. Dock shows `API key rejected; check the key and API restrictions`; no fallback occurs in this mode; log does not loop. |
| 15 | PENDING | Quota error is clear and does not spin | With an exhausted project quota (or a restricted key), connect with `Official API`. Dock shows `YouTube quota exceeded; try No-key or wait until midnight PT`; no tight retry loop. |
| 16 | PENDING | API key never appears in logs or argv | After items 2–4: `grep -R '<first 6 chars of key>' "$LOG"` and the dock log contain nothing; `pgrep -af ytchat-helper` shows no key argument (the key is passed on stdin). |

## 3. Release steps

- [x] CMake project version is `0.1.0`.
- [x] Helper `--version` reports `0.1.0`.
- [x] Metainfo release is `0.1.0` with real release notes (no skeleton text).
- [x] Flatpak manifest installs module, locale, helper, espeak-ng runtime,
      metainfo, licenses and third-party notices.
- [ ] **Tag `v0.1.0` — pending live E2E acceptance.** Do not create the tag
      until Section 2 is executed and its results are recorded here.
      When it passes:
      ```bash
      git tag -a v0.1.0 -m "v0.1.0: Flatpak YouTube chat TTS"
      ```
      There is no remote configured; do not push.
- [ ] Record the Section 2 results and the tagged commit in this file before
      calling the release done.

## 4. Not claimed by this checklist

- Official `streamList` access to public chats not owned by the API key is
  unverified; the no-key provider remains the documented fallback.
- InnerTube is unofficial and may break at any time.
- No interactive OBS item was executed during Task 12 because the user's OBS
  session was in use.
