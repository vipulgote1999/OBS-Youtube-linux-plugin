#!/usr/bin/env bash
set -euo pipefail

# Verify the installed extension from inside the OBS Flatpak sandbox: the
# module, locale, bundled helper, Supertonic model, ONNX Runtime, probe,
# metainfo and license/notice files must all be present, no shared library may
# be missing, and the module must link the bundled ONNX Runtime.
flatpak run --command=sh com.obsproject.Studio -c '
  set -eu
  PREFIX=/app/plugins/YtChatTts
  DATA="$PREFIX/share/obs/obs-plugins/yt-chat-tts"
  MODULE="$PREFIX/lib/obs-plugins/yt-chat-tts.so"
  HELPER="$DATA/bin/ytchat-helper"
  PROBE="$DATA/bin/supertonic-probe"
  LICENSES="$PREFIX/share/licenses/com.obsproject.Studio.Plugin.YtChatTts"
  SUPERTONIC="$DATA/supertonic"
  ONNXRUNTIME="$PREFIX/lib/libonnxruntime-ytc.so.1"

  test -f "$MODULE"
  test -f "$DATA/locale/en-US.ini"

  test -x "$HELPER"
  "$HELPER" --version | grep -q "^ytchat-helper "

  test -x "$PROBE"

  # Supertonic 3: all four ONNX sessions, config/indexer and all ten voices.
  test -f "$SUPERTONIC/onnx/duration_predictor.onnx"
  test -f "$SUPERTONIC/onnx/text_encoder.onnx"
  test -f "$SUPERTONIC/onnx/vector_estimator.onnx"
  test -f "$SUPERTONIC/onnx/vocoder.onnx"
  test -f "$SUPERTONIC/onnx/tts.json"
  test -f "$SUPERTONIC/onnx/unicode_indexer.json"
  for voice in F1 F2 F3 F4 F5 M1 M2 M3 M4 M5; do
    test -f "$SUPERTONIC/voice_styles/$voice.json"
  done
  test -f "$SUPERTONIC/LICENSE"

  test -e "$ONNXRUNTIME"
  test ! -e "$PREFIX/patchelf"

  # Hard-link packaging rule: the plugin must link the bundled ONNX Runtime.
  ldd "$MODULE" | grep -q "libonnxruntime-ytc\.so"

  test -f "$PREFIX/share/metainfo/com.obsproject.Studio.Plugin.YtChatTts.metainfo.xml"
  test -f "$LICENSES/LICENSE"
  test -f "$LICENSES/THIRD_PARTY_NOTICES.md"
  for license in \
    LICENSES/onnxruntime/LICENSE \
    LICENSES/onnxruntime/ThirdPartyNotices.txt \
    LICENSES/supertonic/LICENSE \
    LICENSES/supertonic-3/LICENSE \
    LICENSES/supertonic-3/SOURCE.md \
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

  # ldd on the module resolves the ONNX Runtime dependency too.
  for binary in "$MODULE" "$PROBE"; do
    binary="$(readlink -f "$binary")"
    if ldd "$binary" | grep "not found"; then
      echo "smoke: missing shared library for $binary" >&2
      exit 1
    fi
  done

  # Only runtime files belong in the extension.  Headers and the patchelf
  # helper are build-time artifacts.
  test ! -e "$PREFIX/include"
  test ! -e "$DATA/piper"
  test ! -e "$DATA/espeak-ng-data"
'

echo "smoke: extension installed (module, locale, helper, supertonic, onnxruntime, metainfo, licenses)"
