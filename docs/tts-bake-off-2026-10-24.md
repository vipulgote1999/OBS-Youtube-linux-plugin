# TTS bake-off findings

Date: 2026-10-24. Full numbers: `report.md`, `results.csv`, `listen/index.html`.

## Decision

**Keep Piper.** Neither candidate is faster, and Pocket adds an uncancellable
failure mode. The real latency win is streaming Piper's existing chunked output,
not changing the model.

## Speed (same sherpa-onnx runtime, same corpus, same loaded host)

| engine | median RTF | × realtime | 200-char compute | peak RSS |
|---|---:|---:|---:|---:|
| Piper (VITS `en_US-kristin-medium-int8`) | 0.196 | 5.1× | 2.4 s | 264 MB |
| Pocket TTS (legacy 100M, int8) | 0.44–0.64 | 1.6–2.3× | 4.2 s | 565 MB |
| Kokoro (int8, v0.19) | 1.63–2.02 | **0.5–0.6×** | **12–16 s** | 517–532 MB |
| Shipped Piper (Flatpak, LJSpeech medium) | 0.10–0.13 | 8–10× | 3.2–4.0 s | — |

Kokoro is slower than real time here. That is not a measurement error: sherpa's
own Kokoro test reported RTF 0.86 on a MacBook, and an independent 4-core Xeon
benchmark reported 0.56. Its quality is well regarded, but a 200-character
message needs 12–16 s before playback starts, which is unusable for live chat.

Pocket is 2.3–3.3× slower than Piper on RTF. It produces shorter audio for the
same text (10.8 s vs Piper's 31.7 s for the 200-character phrase), so its
wall-clock per long message is closer than RTF suggests, but it is still slower
on short messages: 0.55–1.94 s vs Piper's 0.13 s.

## Reliability — the disqualifier

Pocket's no-EOS failure (`pocket-tts#113`) reproduced:

- One `"hi"` run produced **39.7 s of audio**.
- One stress run sat inside a single `Generate` call for **>2 minutes at 97 %
  CPU** and never invoked the progress callback. Neither the audio cap nor the
  compute cap could stop it; the process had to be killed. In the plugin this
  would block the TTS worker indefinitely, and `stop()` joins that thread.
- Across the matrix, 3–10 of 23 Pocket rows per voice hit a cap. Some are host
  stalls (16–20 s for ~1 s of audio), which is why `abort_reason` separates
  `audio` from `compute`, but the two-minute hang is a genuine model failure.

Kokoro and Piper completed every row with zero aborts.

## Noise — root cause and fix

The "little noise" was **not** hiss. Quiet regions measured digital silence.
The artifact was a **boundary step**: Pocket never pads silence, so clips start
at full amplitude and often end on a non-zero sample. Measured across 107 dumped
clips, 18 ended above 1 % full scale, the worst at **0.303**. When the audio
buffer runs dry, that step to zero is an audible pop (`pocket-tts#171`).

Fix: `src/speech-conditioner.cpp`, applied to every utterance in
`TtsWorker::run()`:

1. one-pole DC blocker at 20 Hz (Kokoro left a −0.0014 offset);
2. 5 ms raised-cosine fade in and out, so the first and last samples are exactly
   zero;
3. clamp to [−1, 1].

Measured effect on the worst clip (`pocket-mary__uni_cjk`): last sample
`+0.3034 → 0.0000`; mid-speech RMS `0.05260 → 0.05228` (−0.6 %, inaudible).

No noise gate was added. The quiet regions were already silent, so a gate would
add pumping without removing anything real.

## Caveats

- The host was heavily loaded (load average 5–8 on 16 cores, Chrome/QEMU/OBS
  competing). Absolute RTF is inflated; the ratios between rows are the signal.
- The Pocket bundle is the January 2026 legacy checkpoint. The Sept/Oct 2026
  Kyutai checkpoints fix end-of-sentence repetition and number truncation, but
  no working ONNX export exists yet (`PocketTTS.cpp#12`, `sherpa-onnx#3755`).
- Kokoro and Pocket ran through sherpa-onnx v1.13.8, not their upstream Python
  runtimes. A different runtime could be faster; this is what an in-process C++
  integration would actually get.
- Licensing still blocks a Pocket Flatpak: sherpa's model README says
  "non-commercial" next to a CC-BY-4.0 LICENSE (`sherpa-onnx#3971`, open).

## Next step

Implemented on 2026-10-24, after this bake-off:

- `SpeechConditioner` (DC blocker + 5 ms fades) removes the boundary click that
  the raw edge-step numbers above measure.
- **Low-latency streaming** is a dock option, on by default. libpiper emits one
  chunk per sentence, so multi-sentence messages start sooner; a one-sentence
  message is unchanged.
- **Local server** engine talks to a user-run OpenAI-compatible
  `/v1/audio/speech` endpoint. Kokoro and Pocket TTS are available that way
  without bundling their weights or accepting their licence terms in the
  Flatpak. The 24 kHz server audio is resampled with a windowed-sinc filter.
- The default Piper **Speed** moved from `0.85x` to `1.00x`. A saved `0.00` is
  migrated to `1.00` on load: the engine treated `0.00` as one-third speed,
  which is the most likely cause of the "very low quality" report that started
  this investigation.

Pocket is still not bundled: the uncancellable hang and the ambiguous ONNX
bundle licence both point at a separate process, which is what the local-server
engine provides.
