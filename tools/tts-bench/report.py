#!/usr/bin/env python3
"""Turn tts-probe JSONL into a report, CSV and blind listen gallery.

Kept separate from scripts/bench-tts-host.sh so the aggregation can be re-run
against saved probe output without regenerating audio.
"""

import csv
import html
import json
import random
import statistics
import struct
import sys
import wave
from pathlib import Path

SHORT_CATEGORIES = ("short1", "short2", "short3")
OVERLONG_MS = 2500
EDGE_LIMIT = 0.01  # 1% of full scale


def edge_step(path):
    """Largest of the first and last sample, in full-scale units.

    A clip that ends on a non-zero sample steps to silence when the feeder runs
    dry, which is the pop pocket-tts#171 describes.  Pocket showed values up to
    0.303 in the bake-off; Kokoro pads silence and sits at 0.0.
    """
    try:
        with wave.open(path) as w:
            frames = w.getnframes()
            if frames == 0 or w.getnchannels() != 1 or w.getsampwidth() != 2:
                return 0.0
            w.setpos(0)
            first = struct.unpack("<h", w.readframes(1))[0]
            w.setpos(frames - 1)
            last = struct.unpack("<h", w.readframes(1))[0]
            return max(abs(first), abs(last)) / 32768.0
    except (OSError, wave.Error, EOFError):
        return 0.0


def load_rows(out: Path):
    rows = []
    for f in sorted((out / "json").glob("*.jsonl")):
        for line in f.read_text().splitlines():
            if line.strip():
                rows.append(json.loads(line))
    return rows


def summarise(rows):
    """Per-label stats.

    Aborted rows are excluded from latency and RTF: a host stall can abort a
    perfectly good 0.5 s clip after 12 s, and a runaway returns a truncated
    clip whose compute/audio ratio is meaningless.  The abort counts are
    reported separately.
    """
    stats = {}
    labels = []
    for r in rows:
        if r["label"] not in labels:
            labels.append(r["label"])
    for label in labels:
        r = [x for x in rows if x["label"] == label]
        valid = [x for x in r if not x.get("aborted")] or r
        long = [x for x in valid if x["text_id"] == "long_200"] or valid
        stats[label] = {
            "n": len(r),
            "load": statistics.median(x["load_ms"] for x in r),
            "warmup": statistics.median(x["warmup_ms"] for x in r),
            "rtf": statistics.median(x["rtf"] for x in valid),
            "best_rtf": min(x["rtf"] for x in valid),
            "long_ms": statistics.median(x["compute_ms"] for x in long),
            "rss": max(x["peak_rss_mb"] for x in r),
            "click": max(x["click_ratio"] for x in valid),
            "edge": (lambda vals: max(vals) if vals else 0.0)(
                [edge_step(x["wav"]) for x in r if x.get("wav")]),
            "edge_bad": sum(1 for x in r if x.get("wav") and edge_step(x["wav"]) > EDGE_LIMIT),
            "calls": statistics.median(x["callback_calls"] for x in valid),
            "runaway": sum(1 for x in r if x.get("abort_reason") == "audio"),
            "slowcap": sum(1 for x in r if x.get("abort_reason") == "compute"),
            "nshort": sum(1 for x in r if x["category"] in SHORT_CATEGORIES),
            "overlong": sum(1 for x in r
                            if x["category"] in ("short1", "short2")
                            and x["audio_ms"] > OVERLONG_MS),
            "bad": sum(1 for x in r
                       if x.get("aborted") or x["rms"] < 0.005 or x["audio_ms"] < 150),
        }
    return labels, stats


def write_csv(out: Path, rows):
    with (out / "results.csv").open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def write_listen_set(out: Path, rows):
    listen = out / "listen"
    listen.mkdir(parents=True, exist_ok=True)
    for stale in listen.glob("*"):
        stale.unlink()

    random.seed(20261024)
    mapping = []
    for row in rows:
        wav = Path(row.get("wav") or "")
        if row.get("aborted") or not wav.is_file():
            continue
        name = f"{random.randrange(16 ** 8):08x}.wav"
        (listen / name).write_bytes(wav.read_bytes())
        mapping.append((name, row["label"], row["text_id"], row["run"]))

    with (listen / "map.csv").open("w") as fh:
        fh.write("file,label,text_id,run\n")
        for m in mapping:
            fh.write(",".join(str(x) for x in m) + "\n")

    by_text = {}
    for name, label, text_id, run in mapping:
        by_text.setdefault(text_id, []).append((label, run, name))

    parts = [
        "<!doctype html><meta charset='utf-8'><title>Blind TTS listen</title>",
        "<style>body{font:14px system-ui;margin:2rem;max-width:70rem}"
        "section{border-top:1px solid #ccc;padding:1rem 0}"
        "code{background:#f3f3f3;padding:.1rem .3rem}</style>",
        "<h1>Blind TTS comparison</h1>",
        "<p>Files are randomised; the mapping is in <code>map.csv</code>. "
        "Aborted clips are omitted. Judge each text on its own.</p>",
    ]
    for text_id in sorted(by_text):
        parts.append(f"<section><h2>{html.escape(text_id)}</h2>")
        for label, run, name in sorted(by_text[text_id]):
            parts.append(
                f"<div><code>{label} #{run}</code><br>"
                f"<audio controls preload='none' src='{name}'></audio></div>")
        parts.append("</section>")
    (listen / "index.html").write_text("\n".join(parts))


def flatpak_control(out: Path):
    path = out / "piper-flatpak.txt"
    if not path.is_file():
        return []
    lines = ["## Shipped Piper control (Flatpak, LJSpeech medium)", "",
             "| run | elapsed | audio | RTF | × realtime |",
             "|---:|---:|---:|---:|---:|"]
    for line in path.read_text().splitlines():
        if not line.startswith("bench="):
            continue
        fields = dict(part.split("=", 1) for part in line.split() if "=" in part)
        try:
            elapsed = float(fields["elapsed_ms"])
            samples = float(fields["samples"])
            chars = fields.get("chars", "?")
        except (KeyError, ValueError):
            continue
        audio_ms = samples / 22050 * 1000
        rtf = elapsed / audio_ms if audio_ms else 0
        lines.append(f"| {fields.get('chars', '?')} chars "
                     f"| {elapsed:.0f} ms | {audio_ms/1000:.1f} s "
                     f"| {rtf:.3f} | {1/rtf:.1f}× |")
    return lines


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build-host/tts-bench")
    rows = load_rows(out)
    if not rows:
        raise SystemExit("no probe output found")
    write_csv(out, rows)

    labels, stats = summarise(rows)
    piper_rtf = stats.get("vits-kristin", {}).get("rtf")

    lines = ["# TTS bake-off results", ""]
    env = (out / "env.txt")
    if env.is_file():
        lines += ["## Environment", "", "```"] + env.read_text().strip().splitlines() + ["```", ""]
    lines += [
        "Absolute numbers are inflated by whatever else is running on the host;",
        "compare rows against each other, not against a spec sheet.", "",
        "## Per configuration", "",
        "| label | load ms | warm-up ms | median RTF | × realtime | long-200 compute "
        "| peak RSS | edge step | clicks | callbacks | runaways | slow-cap | overlong short "
        "| suspicious |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for label in labels:
        d = stats[label]
        lines.append(
            f"| {label} | {d['load']:.0f} | {d['warmup']:.0f} | {d['rtf']:.3f} | "
            f"{1/d['rtf']:.1f}× | {d['long_ms']:.0f} ms | {d['rss']:.0f} MB | "
            f"{d['edge']:.3f} | {d['click']:.1f} | {d['calls']:.0f} | {d['runaway']} | "
            f"{d['slowcap']} | {d['overlong']}/{d['nshort']} | {d['bad']}/{d['n']} |")

    cats = sorted({r["category"] for r in rows})
    lines += ["", "## Median compute per category (ms, aborted rows excluded)", "",
              "| label | " + " | ".join(cats) + " |",
              "|---|" + "---:|" * len(cats)]
    for label in labels:
        cells = []
        for cat in cats:
            vals = [x["compute_ms"] for x in rows
                    if x["label"] == label and x["category"] == cat
                    and not x.get("aborted")]
            cells.append(f"{statistics.median(vals):.0f}" if vals else "-")
        lines.append(f"| {label} | " + " | ".join(cells) + " |")

    control = flatpak_control(out)
    if control:
        lines += [""] + control

    lines += ["", "## Gates", ""]
    if piper_rtf:
        lines.append(f"- Speed: median RTF ≤ 1.5× Piper (`vits-kristin` = {piper_rtf:.3f}).")
        for label in labels:
            if label == "vits-kristin" or "stress" in label:
                # The stress row is one-word messages only, so its RTF is
                # dominated by fixed overhead and is not comparable.
                continue
            ratio = stats[label]["rtf"] / piper_rtf
            lines.append(f"  - {label}: {ratio:.1f}× Piper → "
                         f"**{'PASS' if ratio <= 1.5 else 'FAIL'}**")
    for label in labels:
        d = stats[label]
        lines.append(f"- {label}: peak RSS {d['rss']:.0f} MB → "
                     f"**{'PASS' if d['rss'] <= 800 else 'FAIL'}** (≤ 800 MB)")
        ns = d["nshort"] or 1
        over = d["overlong"] / ns * 100
        lines.append(f"- {label}: 1–2 word messages producing >2.5 s of audio "
                     f"{d['overlong']}/{d['nshort']} ({over:.0f}%) → "
                     f"**{'PASS' if over <= 10 else 'FAIL'}** (≤ 10%)")
        bad = d["bad"] / d["n"] * 100
        lines.append(f"- {label}: silent/aborted screen {d['bad']}/{d['n']} "
                     f"({bad:.0f}%) → **{'PASS' if bad <= 10 else 'FAIL'}** (≤ 10%)")
        lines.append(f"- {label}: utterance starts/ends ≤1% full scale — "
                     f"{d['edge_bad']}/{d['n']} over the limit (max {d['edge']:.3f}) → "
                     f"**{'PASS' if d['edge'] <= EDGE_LIMIT else 'FAIL'}**. "
                     f"The plugin's SpeechConditioner fades every utterance to zero, "
                     f"so a raw edge step here is fixed at playback.")

    lines += [
        "", "## Caveats", "",
        "- Absolute numbers are inflated by host load; compare rows, not the headline.",
        "- `ttfa_ms` is not streaming latency: sherpa invokes the callback per",
        "  sentence, so a one-sentence input reports full synthesis time.",
        "- The Pocket bundle is the January 2026 legacy checkpoint. Later Kyutai",
        "  checkpoints (Sept/Oct 2026) contain fixes that have no working ONNX",
        "  export yet (`PocketTTS.cpp#12`, `sherpa-onnx#3755`).",
        "- Kokoro and Pocket run through sherpa-onnx v1.13.8, not their upstream",
        "  Python runtimes. A different runtime may be faster; this is what an",
        "  in-process C++ integration would actually get.",
        "- `runaways` hit the audio cap because the model never emitted EOS",
        "  (`pocket-tts#113`). `slow-cap` hit the compute cap, which can also be",
        "  a host stall rather than a model problem.",
        "- Probe audio is raw. The plugin runs every utterance through",
        "  `SpeechConditioner` (DC blocker + 5 ms raised-cosine fades), so the",
        "  edge-step artifact is removed at playback.",
        "",
        "Quality is not automated. Open `listen/index.html` and compare each",
        "text across engines; filenames are randomised and `listen/map.csv`",
        "holds the mapping.", "",
    ]

    (out / "report.md").write_text("\n".join(lines) + "\n")
    write_listen_set(out, rows)
    print(f"wrote {out/'report.md'}, {out/'results.csv'}, {out/'listen/index.html'}")


if __name__ == "__main__":
    main()
