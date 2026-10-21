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
  PROBE="$DATA/bin/piper-probe"
  PIPER_MODEL="$DATA/piper/en_US-ljspeech-medium.onnx"
  PIPER_CONFIG="$DATA/piper/en_US-ljspeech-medium.onnx.json"
  PIPER_ESPEAK_DATA="$DATA/piper/espeak-ng-data"
  LIBPIPER="$PREFIX/lib/libpiper.so"
  ONNXRUNTIME="$PREFIX/lib/libonnxruntime.so.1"

  test -f "$MODULE"
  test -f "$DATA/locale/en-US.ini"

  test -x "$HELPER"
  "$HELPER" --version | grep -q "^ytchat-helper "

  test -x "$ESPEAK"
  test -d "$DATA/espeak-ng-data"

  test -f "$PIPER_MODEL"
  test -f "$PIPER_CONFIG"
  test -d "$PIPER_ESPEAK_DATA"
  test -x "$PROBE"
  test -e "$LIBPIPER"
  test -e "$ONNXRUNTIME"

  test -f "$PREFIX/share/metainfo/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml"
  test -f "$LICENSES/LICENSE"
  test -f "$LICENSES/THIRD_PARTY_NOTICES.md"
  for license in \
    LICENSES/espeak-ng.txt \
    LICENSES/go/genproto-LICENSE \
    LICENSES/go/gjson-LICENSE \
    LICENSES/go/grpc-LICENSE \
    LICENSES/go/grpc-NOTICE.txt \
    LICENSES/go/match-LICENSE \
    LICENSES/go/pretty-LICENSE \
    LICENSES/go/protobuf-LICENSE \
    LICENSES/go/protobuf-PATENTS \
    LICENSES/go/x-net-LICENSE \
    LICENSES/go/x-net-PATENTS \
    LICENSES/go/x-sys-LICENSE \
    LICENSES/go/x-sys-PATENTS \
    LICENSES/go/x-text-LICENSE \
    LICENSES/go/x-text-PATENTS; do
    test -f "$LICENSES/$license"
  done

  for binary in "$MODULE" "$ESPEAK" "$PROBE" "$LIBPIPER"; do
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

echo "smoke: extension installed (module, locale, helper, espeak-ng, piper, metainfo, licenses)"
