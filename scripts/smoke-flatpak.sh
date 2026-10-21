#!/usr/bin/env bash
set -euo pipefail

# Verify the installed extension from inside the OBS Flatpak sandbox: the
# module, locale, bundled helper, bundled espeak-ng runtime, metainfo and
# license/notice files must all be present, and no shared library may be
# missing.
flatpak run --command=sh com.obsproject.Studio -c '
  set -eu
  PREFIX=/app/plugins/YtChatTts
  DATA="$PREFIX/share/obs/obs-plugins/yt-chat-tts"
  MODULE="$PREFIX/lib/obs-plugins/yt-chat-tts.so"
  HELPER="$DATA/bin/ytchat-helper"
  ESPEAK="$DATA/bin/espeak-ng"
  LICENSES="$PREFIX/share/licenses/com.obsproject.Studio.Plugin.YtChatTts"

  test -f "$MODULE"
  test -f "$DATA/locale/en-US.ini"

  test -x "$HELPER"
  "$HELPER" --version | grep -q "^ytchat-helper "

  test -x "$ESPEAK"
  test -d "$DATA/espeak-ng-data"

  test -f "$PREFIX/share/metainfo/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml"
  test -f "$LICENSES/LICENSE"
  test -f "$LICENSES/THIRD_PARTY_NOTICES.md"
  test -f "$LICENSES/LICENSES/espeak-ng.txt"

  for binary in "$MODULE" "$ESPEAK"; do
    if ldd "$binary" | grep "not found"; then
      echo "smoke: missing shared library for $binary" >&2
      exit 1
    fi
  done

  # Only espeak-ng runtime files belong in the extension; its headers and
  # static library are build-time artifacts.
  test ! -e "$PREFIX/include/espeak-ng"
  test ! -e "$PREFIX/lib/libespeak-ng.a"
'

echo "smoke: extension installed (module, locale, helper, espeak-ng, metainfo, licenses)"
