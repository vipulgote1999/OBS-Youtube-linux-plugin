#!/usr/bin/env bash
set -euo pipefail

# Verify the installed extension from inside the OBS Flatpak sandbox: the
# module, locale, bundled helper, bundled espeak-ng runtime, Piper/ONNX
# runtime and LJSpeech voice, metainfo and license/notice files must all be
# present, no shared library may be missing, and the module must actually link
# the bundled Piper runtime (packaging integrity, not a runtime fallback).
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
  ONNXRUNTIME="$PREFIX/lib/libonnxruntime-ytc.so.1"

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
  # The generic ONNX SONAME must not remain: another OBS extension may have
  # loaded a different ABI under that name.
  test ! -e "$PREFIX/lib/libonnxruntime.so"
  test ! -e "$PREFIX/lib/libonnxruntime.so.1"
  test ! -e "$PREFIX/patchelf"

  # The bundled voice is exactly LJSpeech; lessac is research-only and must
  # never ship.  The installed tree also drops Piper build-only files.
  test ! -e "$DATA/piper/en_US-lessac-medium.onnx"
  test ! -e "$DATA/piper/en_US-lessac-medium.onnx.json"
  test ! -e "$PREFIX/bin/piper_exe"
  test ! -e "$PREFIX/include/piper.h"

  # Hard-link packaging rule: the plugin must be linked against the bundled
  # Piper/ONNX runtime.  A module built without it is a packaging failure, not
  # a supported espeak-only runtime mode.
  ldd "$MODULE" | grep -q 'libpiper\.so'
  ldd "$MODULE" | grep -q 'libonnxruntime\.so'

  test -f "$PREFIX/share/metainfo/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml"
  test -f "$LICENSES/LICENSE"
  test -f "$LICENSES/THIRD_PARTY_NOTICES.md"
  for license in \
    LICENSES/espeak-ng.txt \
    LICENSES/espeak-ng-pinned/COPYING \
    LICENSES/espeak-ng-pinned/SOURCE.md \
    LICENSES/ljspeech/MODEL_CARD.md \
    LICENSES/onnxruntime/LICENSE \
    LICENSES/onnxruntime/ThirdPartyNotices.txt \
    LICENSES/piper/COPYING \
    LICENSES/piper/uni_algo-NOTICE.txt \
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

  # ldd on the module resolves the transitive ONNX Runtime dependency too.
  for binary in "$MODULE" "$ESPEAK" "$PROBE" "$LIBPIPER"; do
    # ldd warns about a symlink without following it cleanly; resolve first.
    binary="$(readlink -f "$binary")"
    if ldd "$binary" | grep "not found"; then
      echo "smoke: missing shared library for $binary" >&2
      exit 1
    fi
  done

  # Only runtime files belong in the extension.  espeak-ng headers, static
  # libraries, pkg-config files and piper_exe/piper.h are build-time artifacts.
  test ! -e "$PREFIX/include/espeak-ng"
  test ! -e "$PREFIX/lib/libespeak-ng.a"
  test ! -e "$PREFIX/lib/pkgconfig/espeak-ng.pc"
'

echo "smoke: extension installed (module, locale, helper, espeak-ng, piper, metainfo, licenses)"
