# v0.1.0 Acceptance Checklist

Status legend: `[x]` passed and evidenced in this file, `[ ]` not run.

- **Version:** 0.1.0
- **Branch:** `feature/yt-chat-tts`
- **Headless clean-build gate commit:** `7afa0ba` (extension, scripts and docs
  under test). `d05ba8b` is a README-only follow-up, and the final fix-wave
  commit is the commit containing this checklist (revalidated with the same
  gate; see `task-12-report.md`).
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
  set -eu
  MODULE=/app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so
  HELPER=/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper
  if ldd "$MODULE" | grep "not found"; then
    echo "missing shared library" >&2
    exit 1
  fi
  test -x "$HELPER"
  "$HELPER" --version | grep -q "^ytchat-helper "
'
```

Result: `[x]` exit 0, no missing shared libraries, helper executable, prints
`ytchat-helper 0.1.0`. The `ldd` check exits non-zero on any `not found` line;
there is no `|| true` escape hatch, so a missing library fails the command.

### Installed-layout evidence

Verified by `scripts/smoke-flatpak.sh` inside the OBS sandbox:

- [x] `/app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so`
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/locale/en-US.ini`
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/ytchat-helper` (executable)
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/espeak-ng` (executable)
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/espeak-ng-data/` (directory)
- [x] `/app/plugins/YtChatTts/share/metainfo/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml`
- [x] `/app/plugins/YtChatTts/share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSE`
- [x] `/app/plugins/YtChatTts/share/licenses/com.obsproject.Studio.Plugin.YtChatTts/THIRD_PARTY_NOTICES.md`
- [x] `LICENSES/espeak-ng.txt` and every vendored Go text under `LICENSES/go/`
- [x] `ldd` reports no `not found` for the module or espeak-ng
- [x] Build-only espeak-ng artifacts `include/espeak-ng` and
      `lib/libespeak-ng.a` are absent (smoke check)

Verified by **manual sandbox inspection** on 2026-10-21 (not by
`smoke-flatpak.sh`):

- [x] `espeak-ng-data` contains 488 files, about 13 MB
      (`find .../espeak-ng-data -type f | wc -l`; `du -sh`)
- [x] espeak-ng pkg-config, vim addons and man pages are absent from the
      installed tree (`find /app/plugins/YtChatTts -maxdepth 4`)
- [x] AppStream compose succeeded during the Flatpak build

## 2. Prior helper-level live evidence (Task 5)

Before the OBS UI existed, the helper-level no-key gate was run on a public
live stream on 2026-10-21 with commit `a1549b1` (see `task-5-report.md`):

- `innertube`: connected and returned 73 chat actions in the first response
  (`invalidationContinuationData.timeoutMs = 10000`) for
  `https://www.youtube.com/watch?v=0sfDth4lMLc`.
- `auto` without a key: connected through InnerTube directly and produced 39
  message events; no official-provider request was attempted.

This is CLI evidence for the helper, not for the OBS dock, audio source or
recording. The frontend items below remain **PENDING**.

After the final-review fix wave, the controller reran the helper-level no-key
gate on 2026-10-21 against the same public stream
`https://www.youtube.com/watch?v=0sfDth4lMLc` with `provider=innertube`: the
helper emitted `connecting` → `connected`, delivered real chat messages,
produced no stderr output, and was stopped by the test timeout. This validates
the InnerTube helper path after the liveness/replay changes; it is still **not**
OBS/frontend acceptance.

## 3. Live OBS / provider / recording matrix — PENDING

> **Not run.** The user's OBS session was running throughout Task 12. Per the
> task constraints, OBS was not launched, the session's configuration was not
> touched, the interactive E2E matrix was not executed, and the `v0.1.0` tag
> was not created. Every item in this section is **PENDING**, not passed.

Operator prerequisites:

- OBS Studio Flatpak 32.2.2 with the extension from this commit installed.
- Restart OBS after installing so it loads the new extension.
- A public, currently-live YouTube video URL.
- A second live stream with chat disabled.
- A past VOD and an upcoming premiere for the `not_live` items.
- Optionally a private video URL for the provider-specific error check.
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
| 5 | PENDING | Past VOD and upcoming premiere give `not_live` (both providers); private input differs | **Official API**: a past VOD and an upcoming premiere show `No active live stream found`; no retry loop. **No-key**: a past VOD (even with chat replay) and an upcoming premiere also show `No active live stream found`; no replay messages are spoken. **Private video**: Official currently reports `API key rejected…`/`auth` (permission error), No-key reports `No active live stream found`/`not_live`; record which appears instead of expecting a single `not_live` result. |
| 6 | PENDING | Chat-disabled live stream gives `chat_disabled` (both providers) | **Official API**: connect to a live broadcast with chat off; it has started but has no active chat ID, so the dock shows `Live chat is disabled for this stream`. **No-key**: the same stream's watch page has no chat renderer and shows the same message. No repeated retries in either mode. |
| 7 | PENDING | Stream ends: `ended`, no crash, no orphan helper | While connected to a live stream, let it end (or use a stream ending during the test). Dock shows `Stream ended`; OBS stays responsive. Then `pgrep -af 'ytchat-helper\|espeak-ng'` shows no helper for that stream. |
| 8 | PENDING | High-volume chat stays bounded with a drop summary | Connect to a busy live chat (not a replay) for at least 5 minutes. Dock queue count never exceeds **Queue limit**; memory stays flat. Within about 10 s of drops/evictions the log contains the periodic summary, e.g. `tts: filter dropped 12 message(s) (global-cooldown 7, user-cooldown 5); queue evicted 4 message(s)`, rather than one line per dropped message. `for i in $(seq 1 12); do ps -o rss= -p "$(pgrep -f ytchat-helper)"; sleep 5; done` |
| 9 | PENDING | Long / emoji-only / URL-only / blocklisted messages | Send or observe messages longer than **Max length**, emoji-only, URL-only, and one containing a blocklisted phrase. Long messages are truncated, emoji-only/URL-only/blocklisted are skipped as configured, normal messages still speak. The next drop summary names the filter reasons. |
| 10 | PENDING | OBS exit while connected leaves no processes | While connected and speaking, **File → Exit**. `pgrep -af 'ytchat-helper\|espeak-ng'` prints nothing; no crash dialog; `grep -i 'crash\|segfault' "$LOG"` is clean. |
| 11 | PENDING | Scene switch, source removal, source re-add | Switch between two scenes (source added to all scenes via the dock), then remove the `YouTube Chat TTS` source in the Audio Mixer and add it again with **Sources → +**. OBS does not crash; later messages are audible again; old speech is not replayed. |
| 12 | PENDING | Recording contains the TTS audio | Add the source, click **Test Voice**, record 10 s via **Start Recording**. Play the file and confirm speech is audible; optionally: `ffprobe -v error -show_entries stream=index,codec_type,channels -of compact <recording>` and `ffmpeg -i <recording> -af volumedetect -f null -` (mean volume must not be `-inf`). |
| 13 | PENDING | Settings file mode is `600` | After changing a setting: `stat -c '%a' ~/.var/app/com.obsproject.Studio/config/obs-studio/plugin_config/yt-chat-tts/settings.json` → `600`. |
| 14 | PENDING | Bad key on `Official API` shows `auth`, no retry storm | Set **Provider = Official API**, paste an invalid key. Dock shows `API key rejected; check the key and API restrictions`; no fallback occurs in this mode; log does not loop. |
| 15 | PENDING | Quota error is clear and does not spin | With an exhausted project quota (or a restricted key), connect with `Official API`. Dock shows `YouTube quota exceeded; try No-key or wait until midnight PT`; no tight retry loop. |
| 16 | PENDING | API key never appears in logs or argv | After items 2–4: `grep -R '<first 6 chars of key>' "$LOG"` and the dock log contain nothing; `pgrep -af ytchat-helper` shows no key argument (the key is passed on stdin). |
| 17 | PENDING | Settings persist across an OBS restart | Change voice, rate, filtering and URL; exit OBS; restart. The dock shows the saved values and **Connect automatically** still works if enabled; settings are read from `.../plugin_config/yt-chat-tts/settings.json`. |
| 18 | PENDING | Password field does not reveal the saved key | After saving a key and restarting, the **API key** field is visually masked (password echo); the plaintext key is not shown in the dock, log, or window title. |
| 19 | PENDING | Dock log is capped at 500 lines and clears | Generate more than 500 log lines (busy chat or repeated status changes). The **Log** pane keeps at most 500 lines; **Clear Log** empties it immediately while OBS keeps running. |
| 20 | PENDING | Voice and rate changes are audible without reconnecting | While connected, change **Voice** and **Rate**. The next utterance uses the new values, the connection stays `Connected (…)`, and queued messages are not lost. |

## 3a. Piper TTS engine — headless PASS, live OBS PENDING (post-0.1.0)

This section covers the Piper feature on `feature/piper-tts`. It is **not** part
of the published `v0.1.0` tag and does not modify
`docs/release-notes-v0.1.0.md`.

**Headless evidence** at the commit that adds this section (the commit itself
is `feat: bundle piper tts and document acceptance`; the installed Flatpak
extension used for the sandbox checks was built from the same tree):

| Check | Command | Result |
| --- | --- | --- |
| [x] Host C++ tests | `./scripts/test-host.sh` | CTest **11/11 passed**, including `test_piper_engine`, `test_tts_backend` and `test_settings` |
| [x] Go helper | `cd helper && go test -count=1 ./... && go vet ./...` | All packages pass; `go vet` exit 0 |
| [x] Flatpak build | `PATH=/tmp/opencode/toolchain/go/bin:$PATH ./scripts/build-flatpak.sh` | Exit 0; extension installed |
| [x] Installed-layout smoke | `./scripts/smoke-flatpak.sh` | `smoke: extension installed (module, locale, helper, espeak-ng, piper, metainfo, licenses)` |
| [x] espeak-ng regression | `./scripts/test-espeak-flatpak.sh` | `eSpeak NG text-to-speech: 1.51`; WAV RIFF check passes |
| [x] Piper synthesis + latency + fallback probes | `./scripts/test-piper-flatpak.sh` | Exit 0; see timings below |
| [x] No missing shared libraries | `flatpak run --command=sh com.obsproject.Studio -c 'ldd ... \| grep "not found" && exit 1 \|\| true'` | Exit 0 for the module; `ldd` also resolves libpiper/ONNX Runtime |
| [x] Model and config present in sandbox | same command: `test -f .../piper/en_US-ljspeech-medium.onnx[.json]` | Exit 0 |

Installed assets verified by `smoke-flatpak.sh` and sandbox inspection:

- [x] `/app/plugins/YtChatTts/lib/libpiper.so` and `libonnxruntime.so*`; the
      installed module links both (`ldd` assertions in the smoke script).
- [x] `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/piper/`
      contains `en_US-ljspeech-medium.onnx` (~61 MiB on disk), its
      `.onnx.json`, and a separate `espeak-ng-data/` (~19 MiB).
- [x] `en_US-lessac-medium` is absent (not bundled).
- [x] Piper build-only `bin/piper_exe` and `include/piper.h` are pruned from
      the installed extension.
- [x] Licenses installed under
      `share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/`:
      `piper/COPYING`, `piper/uni_algo-NOTICE.txt`, `onnxruntime/LICENSE`,
      `onnxruntime/ThirdPartyNotices.txt`, `ljspeech/MODEL_CARD.md`,
      `espeak-ng-pinned/COPYING`, `espeak-ng-pinned/SOURCE.md`, plus the
      existing espeak-ng and Go texts.
- [x] Installed extension size is **139.5 MB** (`flatpak info`), versus the
      espeak-only extension; the model is ~61 MiB and ONNX Runtime ~21 MiB.
- [x] The `piper-probe` installed RPATH now resolves the bundled libraries via
      `$ORIGIN/../../../../../lib`.

Performance (deterministic 200-character phrase, three runs in one process
after two short warm-up utterances; model loaded once):

| Measurement | Result |
| --- | --- |
| Model load + first short utterance (`synthesis=1`) | 662 ms |
| Second short utterance (`synthesis=2`) | 105 ms |
| 200-char run 1 (warm-up) | 1184 ms |
| 200-char run 2 (steady) | 1185 ms |
| 200-char run 3 (steady) | 1131 ms |
| Best steady-state run | **1131 ms — under the 1.5 s reference target** |
| Output format | 22,050 Hz mono float, positive sample counts (268k–271k for 200 chars) |

The model is created on the first utterance and reused for the rest of the
process; the probe would fail if any run returned a non-22,050 Hz rate or zero
samples.

**Live OBS acceptance — PENDING.** OBS was not launched and its configuration
was not touched. Do not mark these passed until an operator runs them and
records the date and result:

| # | Status | Check | How to run / expected result |
| --- | --- | --- | --- |
| P1 | PENDING | Extension reloads with Piper | Restart OBS, then click **Test Voice** or **Connect** (the worker is created on first speech use). Log shows `tts engine: piper (en_US-ljspeech-medium)`, the model/config/data paths, and no error. |
| P2 | PENDING | Test Voice with Piper | Select **Engine = Piper — natural (CPU)**, click **Test Voice**. Audio Mixer shows a level and the sentence is audible; log has no fallback line. |
| P3 | PENDING | Rate mapping is audible | Change **Rate** (for example 120 → 300). The next Test Voice utterance changes speed, the queue survives, and the connection (if any) stays up. |
| P4 | PENDING | Live chat message is spoken with Piper | Connect to a public live chat, confirm real messages are spoken with the natural voice, and no model reload occurs per message (no repeated multi-second stalls). |
| P5 | PENDING | Engine switch to espeak-ng | While connected, select **espeak-ng — fast fallback**. The next utterance uses the espeak voice; the dock does not reconnect; switching back to Piper works. |
| P6 | PENDING | Missing-model fallback | In a test build (never the user's install), rename `piper/en_US-ljspeech-medium.onnx` or set an invalid `piperModelPath`, then restart OBS/Test Voice. The dock logs the exact missing path and speaks through espeak-ng; OBS stays responsive. Restore the file afterwards. |
| P7 | PENDING | Corrupt-model/init fallback | Point `piperModelPath` at a copy of the config or a truncated model. The dock logs a Piper load error and falls back to espeak-ng; no crash, no silence, no retry storm per message. |
| P8 | PENDING | 200-character latency in OBS | Send/Test Voice a 200-character message. Speech starts promptly after the one-time load; measure wall-clock from queue entry to first audio and record it. The headless reference is 1.13 s steady-state on this CPU. |
| P9 | PENDING | CPU and memory observation | During a busy chat with Piper selected, record OBS CPU and RSS for at least 5 minutes (`ps -o %cpu,rss -p "$(pgrep -f obs)"`). Memory should stay flat after the model load; CPU should track message volume, not grow per message. |
| P10 | PENDING | No library/process leaks after exit | While connected and speaking, **File → Exit**. `pgrep -af 'ytchat-helper\|espeak-ng\|piper'` prints nothing; no crash; no `not found`/library errors in the OBS log. |

## 4. Release steps

- [x] CMake project version is `0.1.0`.
- [x] Helper `--version` reports `0.1.0`.
- [x] Metainfo release is `0.1.0` with real release notes (no skeleton text).
- [x] Flatpak manifest installs module, locale, helper, espeak-ng runtime,
      metainfo, licenses and third-party notices.
- [ ] **Tag `v0.1.0` — pending live E2E acceptance.** Do not create the tag
      until Section 3 is executed and its results are recorded here.
      When it passes:
      ```bash
      git tag -a v0.1.0 -m "v0.1.0: Flatpak YouTube chat TTS"
      ```
      There is no remote configured; do not push.
- [ ] Record the Section 3 results and the tagged commit in this file before
      calling the release done.

## 5. Known limitations and not claimed

- **UI responsiveness:** `ChatClient` starts and stops the helper synchronously
  on the OBS UI thread, and engine-setting changes restart the speech worker.
  Connecting can block briefly while the helper starts; disconnecting or
  unloading can wait for process termination. A hung helper can delay OBS for up
  to a few seconds (5 s start wait plus up to 2 s terminate and 2 s kill grace).
  This is known inherited debt, not fixed in v0.1.0.
- Official `streamList` access to public chats not owned by the API key is
  unverified; the no-key provider remains the documented fallback.
- InnerTube is unofficial and may break at any time. Live-chat replay is
  intentionally unsupported: past VODs are rejected as `not_live`.
- No interactive OBS item was executed during Task 12/its fix wave because the
  user's OBS session was in use.
