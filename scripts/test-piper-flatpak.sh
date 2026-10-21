#!/usr/bin/env bash
set -euo pipefail

# Run the installed Piper probe inside the OBS Flatpak sandbox with the bundled
# voice and espeak-ng data.  Fails unless libpiper reports 22,050 Hz and a
# positive sample count.
set +e
output="$(flatpak run --command=sh com.obsproject.Studio -c '
  set -eu
  PREFIX=/app/plugins/YtChatTts
  DATA="$PREFIX/share/obs/obs-plugins/yt-chat-tts"
  PROBE="$DATA/bin/piper-probe"
  MODEL="$DATA/piper/en_US-ljspeech-medium.onnx"
  CONFIG="$DATA/piper/en_US-ljspeech-medium.onnx.json"
  ESPEAK_DATA="$DATA/piper/espeak-ng-data"

  test -x "$PROBE"
  test -f "$MODEL"
  test -f "$CONFIG"
  test -d "$ESPEAK_DATA"

  "$PROBE" --model "$MODEL" --config "$CONFIG" --espeak-data "$ESPEAK_DATA"
')"
status=$?
set -e

echo "${output}"
if [[ ${status} -ne 0 ]]; then
    echo "piper: probe failed with exit code ${status}" >&2
    exit "${status}"
fi

grep -q 'sample_rate=22050' <<<"${output}"
samples="$(sed -n 's/.*samples=\([0-9][0-9]*\).*/\1/p' <<<"${output}" | tail -n 1)"
if [[ -z "${samples}" || "${samples}" -le 0 ]]; then
    echo "piper: expected a positive sample count, got '${samples:-none}'" >&2
    exit 1
fi

echo "piper: sample_rate=22050 samples=${samples}"
