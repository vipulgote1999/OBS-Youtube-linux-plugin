#!/usr/bin/env bash
set -euo pipefail
flatpak run --command=sh com.obsproject.Studio -c '
  set -eu
  BIN=/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/espeak-ng
  DATA=/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/espeak-ng-data
  test -x "$BIN"
  test -d "$DATA"
  ESPEAK_DATA_PATH="$DATA" "$BIN" --version
  ESPEAK_DATA_PATH="$DATA" "$BIN" --stdout -v en-us "hello from espeak" > /tmp/espeak-test.wav
  test "$(wc -c < /tmp/espeak-test.wav)" -gt 4000
  head -c 4 /tmp/espeak-test.wav | grep -q RIFF
  if ldd "$BIN" | grep -q "not found"; then exit 1; fi
'
