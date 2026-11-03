#!/usr/bin/env bash
set -euo pipefail

# Run the installed Supertonic probe inside the OBS Flatpak sandbox with the
# bundled model and voice styles.  Checks, in order:
#   1. a short synthesis through the shipped SupertonicEngine at 22,050 Hz
#      with a positive sample count (the model emits 44,100 Hz and the engine
#      resamples);
#   2. a warm-up plus three timed 200-character runs, proving the model is
#      loaded once and reused;
#   3. cancellation before the first utterance returns a "cancelled" error
#      without loading the model (a nonexistent model directory is passed);
#   4. a missing voice/model fails cleanly with a descriptive error instead of
#      aborting the process.
#
# There is deliberately no steady-state timing gate here: Task 8 records the
# measured RTF on the reference machine, and a hard threshold would make the
# packaging test flaky on slower CPUs.  The overall run is still bounded so a
# hang is reported instead of blocking forever.

set +e
output="$(timeout 900 flatpak run --command=sh com.obsproject.Studio -c '
  set -eu
  PREFIX=/app/plugins/YtChatTts
  DATA="$PREFIX/share/obs/obs-plugins/yt-chat-tts"
  PROBE="$DATA/bin/supertonic-probe"
  MODEL="$DATA/supertonic"

  test -x "$PROBE"
  test -f "$MODEL/onnx/vector_estimator.onnx"
  test -f "$MODEL/voice_styles/F1.json"

  "$PROBE" --model-dir "$MODEL" --voice F1 --steps 8 \
    --text "Supertonic packaging probe."
  "$PROBE" --model-dir "$MODEL" --voice M3 --steps 4 --bench

  # A cancel that lands before synthesis must not load the model, so this uses
  # a deliberately nonexistent directory.
  "$PROBE" --model-dir "$DATA/definitely-missing-supertonic" --voice F1 --cancel
' 2>&1)"
status=$?
set -e

echo "${output}"
if [[ ${status} -ne 0 ]]; then
    echo "supertonic: probe failed with exit code ${status}" >&2
    exit "${status}"
fi

if ! grep -qE '^synthesis=1 sample_rate=22050 samples=[1-9][0-9]* elapsed_ms=[0-9]+ chars=[0-9]+$' \
    <<<"${output}"; then
    echo "supertonic: expected a successful sample_rate=22050 synthesis line" >&2
    exit 1
fi

for run in 0 1 2 3; do
    if ! grep -qE "^bench=${run} sample_rate=22050 samples=[1-9][0-9]* elapsed_ms=[0-9]+ chars=200$" \
        <<<"${output}"; then
        echo "supertonic: expected a successful 200-character bench=${run} line" >&2
        exit 1
    fi
done

grep -q '^cancel=ok error=cancelled$' <<<"${output}"

# The first synthesis includes the model load; peak RSS must be measured.
if ! grep -qE '^peak_rss_mb=[1-9][0-9]*(\.[0-9]+)?$' <<<"${output}"; then
    echo "supertonic: expected a positive peak_rss_mb measurement" >&2
    exit 1
fi

# A missing model must fail with a diagnostic, not a crash.  This path is
# separate so it does not affect the success-output checks above.
set +e
bad_output="$(timeout 120 flatpak run --command=sh com.obsproject.Studio -c '
  set -u
  PREFIX=/app/plugins/YtChatTts
  DATA="$PREFIX/share/obs/obs-plugins/yt-chat-tts"
  exec "$DATA/bin/supertonic-probe" \
    --model-dir "$DATA/definitely-missing-supertonic" --voice F1 \
    --text "This must fail."
' 2>&1)"
bad_status=$?
set -e

echo "${bad_output}"
if [[ ${bad_status} -eq 0 ]]; then
    echo "supertonic: missing-model probe unexpectedly succeeded" >&2
    exit 1
fi
if [[ ${bad_status} -eq 134 ]] || grep -qiE 'terminate called|Aborted|SIGABRT' <<<"${bad_output}"; then
    echo "supertonic: missing-model probe crashed instead of reporting an error" >&2
    exit 1
fi
if ! grep -qi 'error:' <<<"${bad_output}"; then
    echo "supertonic: missing-model probe did not report a descriptive error" >&2
    exit 1
fi

samples="$(sed -n 's/^synthesis=1 .*samples=\([0-9][0-9]*\).*/\1/p' <<<"${output}" | head -n 1)"
echo "supertonic: sample_rate=22050 samples=${samples} bench=ok cancel=ok missing-model=clean"
