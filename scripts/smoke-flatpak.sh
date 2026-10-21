#!/usr/bin/env bash
set -euo pipefail

flatpak run --command=sh com.obsproject.Studio -c '
  test -f /app/plugins/YtChatTts/lib/obs-plugins/yt-chat-tts.so
  test -f /app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/locale/en-US.ini
'

echo "smoke: extension installed"
