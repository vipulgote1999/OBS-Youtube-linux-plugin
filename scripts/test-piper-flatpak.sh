#!/usr/bin/env bash
set -euo pipefail

# Run the installed Piper probe inside the OBS Flatpak sandbox with the bundled
# voice and espeak-ng data.  Checks, in order:
#   1. raw libpiper C API synthesis at 22,050 Hz with a positive sample count;
#   2. the shipped LibPiperApi/PiperEngine backend (two utterances in one
#      process, so the model is created once and reused);
#   3. cancellation before the first utterance returns a "cancelled" error
#      without loading the model (a nonexistent model path is passed);
#   4. a nonexistent config path fails cleanly with a descriptive error instead
#      of aborting the process (SIGABRT / exit 134).

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

  "$PROBE" --model "$MODEL" --config "$CONFIG" --espeak-data "$ESPEAK_DATA" --engine c-api
  "$PROBE" --model "$MODEL" --config "$CONFIG" --espeak-data "$ESPEAK_DATA" --engine backend

  # A cancel that lands before synthesis must not create the synthesizer, so
  # this uses a deliberately nonexistent model.
  "$PROBE" --model "$DATA/piper/definitely-missing.onnx" --config "$CONFIG" \
    --espeak-data "$ESPEAK_DATA" --engine backend --cancel
' 2>&1)"
status=$?
set -e

echo "${output}"
if [[ ${status} -ne 0 ]]; then
    echo "piper: probe failed with exit code ${status}" >&2
    exit "${status}"
fi

grep -q 'sample_rate=22050' <<<"${output}"
grep -q 'synthesis=2 sample_rate=22050' <<<"${output}"
grep -q 'cancel=ok error=cancelled' <<<"${output}"
samples="$(sed -n 's/.*samples=\([0-9][0-9]*\).*/\1/p' <<<"${output}" | head -n 1)"
if [[ -z "${samples}" || "${samples}" -le 0 ]]; then
    echo "piper: expected a positive sample count, got '${samples:-none}'" >&2
    exit 1
fi

check_expected_failure() {
    local name="$1"
    local exit_code="$2"
    local probe_output="$3"

    echo "${probe_output}"
    if [[ ${exit_code} -eq 0 ]]; then
        echo "piper: ${name} unexpectedly succeeded" >&2
        return 1
    fi
    if [[ ${exit_code} -eq 134 ]]; then
        echo "piper: ${name} aborted with SIGABRT (exit 134)" >&2
        return 1
    fi
    if grep -qiE 'terminate called|Aborted|SIGABRT' <<<"${probe_output}"; then
        echo "piper: ${name} crashed instead of reporting an error" >&2
        return 1
    fi
    if ! grep -qi 'error:' <<<"${probe_output}"; then
        echo "piper: ${name} did not report a descriptive error" >&2
        return 1
    fi

    echo "piper: ${name} failed cleanly (exit ${exit_code})"
}

set +e
bad_capi="$(flatpak run --command=sh com.obsproject.Studio -c '
  set -u
  PREFIX=/app/plugins/YtChatTts
  DATA="$PREFIX/share/obs/obs-plugins/yt-chat-tts"
  exec "$DATA/bin/piper-probe" \
    --model "$DATA/piper/en_US-ljspeech-medium.onnx" \
    --config "$DATA/piper/definitely-missing.onnx.json" \
    --espeak-data "$DATA/piper/espeak-ng-data" \
    --engine c-api
' 2>&1)"
bad_capi_status=$?

bad_backend="$(flatpak run --command=sh com.obsproject.Studio -c '
  set -u
  PREFIX=/app/plugins/YtChatTts
  DATA="$PREFIX/share/obs/obs-plugins/yt-chat-tts"
  exec "$DATA/bin/piper-probe" \
    --model "$DATA/piper/en_US-ljspeech-medium.onnx" \
    --config "$DATA/piper/definitely-missing.onnx.json" \
    --espeak-data "$DATA/piper/espeak-ng-data" \
    --engine backend
' 2>&1)"
bad_backend_status=$?
set -e

check_expected_failure "C API missing config" "${bad_capi_status}" "${bad_capi}"
check_expected_failure "backend missing config" "${bad_backend_status}" "${bad_backend}"

echo "piper: sample_rate=22050 samples=${samples} cancel=ok missing-config=clean"
