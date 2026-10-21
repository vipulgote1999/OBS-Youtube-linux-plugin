#!/usr/bin/env bash
set -euo pipefail

# Run the installed Piper probe inside the OBS Flatpak sandbox with the bundled
# voice and espeak-ng data.  Checks, in order:
#   1. raw libpiper C API synthesis at 22,050 Hz with a positive sample count;
#   2. the shipped LibPiperApi/PiperEngine backend (two short utterances plus
#      three timed runs of a deterministic 200-character phrase) in one process,
#      so the model is created once and reused;
#   3. the best steady-state 200-character run completes in under 1.5 s;
#   4. cancellation before the first utterance returns a "cancelled" error
#      without loading the model (a nonexistent model path is passed);
#   5. a nonexistent config path fails cleanly with a descriptive error instead
#      of aborting the process (SIGABRT / exit 134).
#
# The 1.5 s target is the plan's reference-CPU target for steady-state
# synthesis; it excludes the one-time model load and the first utterance.
# The overall sandbox run gets a generous timeout so a slow machine reports a
# timing failure rather than an opaque hang.

STEADY_STATE_TARGET_MS=1500

set +e
output="$(timeout 600 flatpak run --command=sh com.obsproject.Studio -c '
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
  "$PROBE" --model "$MODEL" --config "$CONFIG" --espeak-data "$ESPEAK_DATA" --engine backend \
    --bench

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

# Anchor the raw C API line: an unanchored match could pass on the backend or
# benchmark lines even if the C API path regressed.
capi_line="$(grep -E '^sample_rate=22050 samples=[0-9]+$' <<<"${output}" | head -n 1)"
if [[ -z "${capi_line}" ]]; then
    echo "piper: expected a 'sample_rate=22050 samples=<count>' C API line" >&2
    exit 1
fi
samples="${capi_line##*samples=}"
if [[ "${samples}" -le 0 ]]; then
    echo "piper: expected a positive sample count, got '${samples}'" >&2
    exit 1
fi

grep -q 'synthesis=2 sample_rate=22050' <<<"${output}"
for run in 1 2 3; do
    if ! grep -qE "^bench=${run} sample_rate=22050 samples=[1-9][0-9]* elapsed_ms=[0-9]+ chars=200$" \
        <<<"${output}"; then
        echo "piper: expected a successful 200-character bench=${run} line" >&2
        exit 1
    fi
done

# Use the best steady-state run (bench=2 or bench=3; bench=1 is warm-up) so a
# single scheduler hiccup does not fail the reference-CPU target.
steady_ms=""
for run in 2 3; do
    run_ms="$(sed -n "s/^bench=${run} .*elapsed_ms=\([0-9][0-9]*\).*/\1/p" <<<"${output}")"
    if [[ -z "${run_ms}" ]]; then
        echo "piper: could not read the steady-state timing from bench=${run}" >&2
        exit 1
    fi
    if [[ -z "${steady_ms}" ]] || (( run_ms < steady_ms )); then
        steady_ms="${run_ms}"
    fi
done
if (( steady_ms >= STEADY_STATE_TARGET_MS )); then
    echo "piper: steady-state 200-character synthesis took ${steady_ms} ms;" \
        "target is under ${STEADY_STATE_TARGET_MS} ms" >&2
    exit 1
fi

grep -q 'cancel=ok error=cancelled' <<<"${output}"

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

echo "piper: sample_rate=22050 samples=${samples} bench_steady_ms=${steady_ms} cancel=ok missing-config=clean"
