#!/usr/bin/env bash
set -euo pipefail

# Host-side TTS bake-off: Piper (VITS), Kokoro and Pocket TTS through the same
# sherpa-onnx runtime.
#
#   ./scripts/bench-tts-host.sh \
#       --cmake /path/to/cmake \
#       --proxy https://gh-proxy.com        # only if GitHub releases stall
#
# Every model archive is pinned by SHA-256, so a proxy cannot alter the input.
# Outputs land in build-host/tts-bench/:
#   report.md      gate-by-gate verdict
#   results.csv    one row per utterance
#   json/*.jsonl   raw probe output
#   wav/           deterministic names
#   listen/        randomised names + index.html for a blind listen
#
# This is a development tool. It never builds or modifies the plugin.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build-host/tts-bench"
DEPS=""
CMAKE="${CMAKE:-cmake}"
PROXY=""
RUNS=1
SKIP_DOWNLOAD=0
SKIP_FLATPAK=0

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<'EOF'

Options:
  --deps DIR       reuse/extract model archives here (default: <out>/deps)
  --out DIR        output directory (default: build-host/tts-bench)
  --cmake PATH     cmake binary
  --proxy URL      prepend URL when downloading GitHub release assets
  --runs N         repeats per utterance (default 1)
  --skip-download  fail instead of downloading missing archives
  --skip-flatpak   do not run the shipped Piper control row
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --deps) DEPS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --cmake) CMAKE="$2"; shift 2 ;;
        --proxy) PROXY="$2"; shift 2 ;;
        --runs) RUNS="$2"; shift 2 ;;
        --skip-download) SKIP_DOWNLOAD=1; shift ;;
        --skip-flatpak) SKIP_FLATPAK=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

DEPS="${DEPS:-$OUT/deps}"
mkdir -p "$OUT"/{json,wav,listen} "$DEPS/ext"

SHERPA_SHA=c0bdb7907d3a74bba1d55d22bf4d9fa75586cf1530614ebe88a27b9118e015c4
POCKET_SHA=2f3b88823cbbb9bf0b2477ec8ae7b3fec417b3a87b6bb5f256dba66f2ad967cb
KOKORO_SHA=c9f0dd393615805b0bab050c340834d5e684e732aec91c0e860cd30e982c08bd
PIPER_SHA=16289d7ee8e6b2311a0a0af6531a55f498f82499644a1bb6fddb991fe6fa950c
VOICE_ANNA_SHA=0a6de25cf12bf1540beb85979f306a92be81fecc051c547c5395e7e5237a3856
VOICE_MARY_SHA=a35b0468382218e9f37a9a7494d1e4b74deaf18d7ced22265b4e325bb55c183f

fetch() {
    local url="$1" out="$2" want="$3"
    if [[ -f "$out" ]] && [[ "$(sha256sum "$out" | cut -d' ' -f1)" == "$want" ]]; then
        echo "cached  $(basename "$out")"
        return
    fi
    if [[ $SKIP_DOWNLOAD -eq 1 ]]; then
        echo "missing $(basename "$out") and --skip-download was given" >&2
        exit 1
    fi
    echo "fetch   $(basename "$out")"
    local direct="$url"
    local proxied="$url"
    if [[ -n "$PROXY" ]]; then
        proxied="$PROXY/$url"
    fi
    # Try direct first, then the proxy.  A stalled direct download is common on
    # networks that throttle release-assets.githubusercontent.com.
    curl -L --fail --retry 2 --connect-timeout 15 --max-time 900 -o "$out" "$proxied" \
        || curl -L --fail --retry 2 --connect-timeout 15 --max-time 900 -o "$out" "$direct"
    local got
    got="$(sha256sum "$out" | cut -d' ' -f1)"
    if [[ "$got" != "$want" ]]; then
        echo "sha256 mismatch for $out" >&2
        echo "  got  $got" >&2
        echo "  want $want" >&2
        exit 3
    fi
    echo "ok      $(basename "$out")"
}

GH=https://github.com/k2-fsa/sherpa-onnx/releases/download
echo "== downloads =="
fetch "$GH/v1.13.8/sherpa-onnx-v1.13.8-linux-x64-shared.tar.bz2" \
      "$DEPS/sherpa.tar.bz2" "$SHERPA_SHA"
fetch "$GH/tts-models/sherpa-onnx-pocket-tts-int8-2026-01-26.tar.bz2" \
      "$DEPS/pocket.tar.bz2" "$POCKET_SHA"
fetch "$GH/tts-models/kokoro-int8-en-v0_19.tar.bz2" \
      "$DEPS/kokoro.tar.bz2" "$KOKORO_SHA"
fetch "$GH/tts-models/vits-piper-en_US-kristin-medium-int8.tar.bz2" \
      "$DEPS/piper-kristin.tar.bz2" "$PIPER_SHA"

mkdir -p "$DEPS/voices"
fetch "https://huggingface.co/kyutai/tts-voices/resolve/main/vctk/p228_023_enhanced.wav" \
      "$DEPS/voices/anna.wav" "$VOICE_ANNA_SHA"
fetch "https://huggingface.co/kyutai/tts-voices/resolve/main/vctk/p333_023_enhanced.wav" \
      "$DEPS/voices/mary.wav" "$VOICE_MARY_SHA"

echo "== extract =="
for archive in sherpa pocket kokoro piper-kristin; do
    if [[ ! -e "$DEPS/ext/.$archive" ]]; then
        tar xf "$DEPS/$archive.tar.bz2" -C "$DEPS/ext"
        touch "$DEPS/ext/.$archive"
    fi
done

SHERPA="$DEPS/ext/sherpa-onnx-v1.13.8-linux-x64-shared"
POCKET="$DEPS/ext/sherpa-onnx-pocket-tts-int8-2026-01-26"
KOKORO="$DEPS/ext/kokoro-int8-en-v0_19"
VITS="$DEPS/ext/vits-piper-en_US-kristin-medium-int8"

echo "== build probe =="
"$CMAKE" -S "$ROOT/tools/tts-bench" -B "$OUT/build" \
    -DCMAKE_BUILD_TYPE=Release -DSHERPA_ONNX_ROOT="$SHERPA" >/dev/null
"$CMAKE" --build "$OUT/build" -j "$(nproc)"

PROBE="$OUT/build/tts-probe"
CORPUS="$ROOT/tools/tts-bench/corpus.tsv"

{
    echo "date: $(date -Is)"
    echo "nproc: $(nproc)"
    echo "loadavg: $(cut -d' ' -f1-3 /proc/loadavg)"
    echo "mem: $(free -m | awk '/Mem:/{print $2"MB total, "$7"MB available"}')"
    echo "cpu: $(lscpu | sed -n 's/^Model name: *//p' | head -1)"
} > "$OUT/env.txt"

run_one() {
    local label="$1" engine="$2" model="$3" voice="$4" threads="$5"
    shift 5
    echo "== $label (engine=$engine threads=$threads) =="
    # A single utterance can hang inside Generate without ever invoking the
    # progress callback, so the per-utterance caps cannot stop it.  The process
    # timeout is the only backstop; partial JSONL is kept for the report.
    timeout "${PROBE_TIMEOUT:-900}" "$PROBE" --engine "$engine" --model-dir "$model" \
        --voice "$voice" --text-file "$CORPUS" --runs "$RUNS" --threads "$threads" \
        --num-steps 1 --wav-dir "$OUT/wav" --label "$label" \
        "$@" > "$OUT/json/$label.jsonl" 2> "$OUT/json/$label.log" || {
        echo "   warning: $label timed out or failed; keeping partial output" >&2
    }
    tail -2 "$OUT/json/$label.log" | sed 's/^/   /'
}

# Pocket is fastest on one thread: 4-8 threads made it 4x slower in testing
# (ORT oversubscription).  Kokoro peaks at 4.  Each engine runs at its own
# best-known configuration; that is what we would ship.
#
# Pocket also occasionally never emits EOS (pocket-tts#113) and would generate
# 40 s of audio for a two-letter message.  The cap turns that into a recorded
# failure instead of an hour-long run.
POCKET_CAP=(--max-audio-seconds 12 --max-compute-seconds 12)
run_one pocket-bria   pocket "$POCKET" "$POCKET/test_wavs/bria.wav" 1 "${POCKET_CAP[@]}"
run_one pocket-anna   pocket "$POCKET" "$DEPS/voices/anna.wav" 1 "${POCKET_CAP[@]}"
run_one pocket-mary   pocket "$POCKET" "$DEPS/voices/mary.wav" 1 "${POCKET_CAP[@]}"
run_one kokoro-sid0   kokoro "$KOKORO" 0 4 --max-audio-seconds 30 --max-compute-seconds 30
run_one kokoro-sid1   kokoro "$KOKORO" 1 4 --max-audio-seconds 30 --max-compute-seconds 30
run_one kokoro-sid3   kokoro "$KOKORO" 3 4 --max-audio-seconds 30 --max-compute-seconds 30
run_one vits-kristin  vits   "$VITS"   0 2

# Short-message stress: the disqualifying gate needs a denominator.  Six runs
# of each one-word message, capped tightly so a runaway cannot dominate.
awk -F'\t' 'BEGIN{OFS="\t"} $1 ~ /^#/ {print} $2 == "short1" {print}' \
    "$CORPUS" > "$OUT/corpus-short1.tsv"
run_one pocket-short-stress pocket "$POCKET" "$POCKET/test_wavs/bria.wav" 1 \
    --text-file "$OUT/corpus-short1.tsv" --runs 6 \
    --max-audio-seconds 8 --max-compute-seconds 8

if [[ $SKIP_FLATPAK -eq 0 ]] && command -v flatpak >/dev/null 2>&1 \
   && flatpak info com.obsproject.Studio.Plugin.YtChatTts >/dev/null 2>&1; then
    echo "== shipped Piper control (Flatpak) =="
    flatpak run --command=sh com.obsproject.Studio -c '
        set -eu
        D=/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts
        "$D/bin/piper-probe" --model "$D/piper/en_US-ljspeech-medium.onnx" \
            --config "$D/piper/en_US-ljspeech-medium.onnx.json" \
            --espeak-data "$D/piper/espeak-ng-data" --engine backend --bench' \
        > "$OUT/piper-flatpak.txt" 2>&1 || true
    grep -E '^bench=' "$OUT/piper-flatpak.txt" | sed 's/^/   /' || true
fi

echo "== aggregate =="
python3 "$ROOT/tools/tts-bench/report.py" "$OUT"

echo
echo "Report: $OUT/report.md"
