# Supertonic 3 evaluation

Date: 2026-11-03. Model: Supertonic 3, Hugging Face revision
`aafc6e32416a594460b32413efc49d7fe4ce6d46`. Engine: in-process
`LibSupertonicApi` + `SupertonicEngine` on ONNX Runtime 1.22.0 (CPU).
Voice: `F1`. Speed: `1.00×`. Output: 22,050 Hz mono float.

## Decision

Supertonic 3 is the bundled engine. **Piper and espeak-ng were removed** after
this evaluation (2026-11-03); the local OpenAI-compatible server engine
remains as the alternative. Supertonic is the default, with `F1`, 8 denoising
steps and `1.00×` defaults. The UI exposes 0.90–1.50× because lower values add
long pauses instead of slowing the words proportionally.

There is no automatic fallback engine: a missing or corrupt Supertonic model
stops the worker with a readable error. This is documented in the README and
the 0.3.0 release metadata.

## OBS playback speed: duplicate source consumers (fixed)

The reported "sped up" audio was not the model or the 44.1→22.05 kHz
resampler. The scene collection contained four `YouTube Chat TTS` sources
(`YouTube Chat TTS`, `2`, `3`, `4`), and every source instance started its
own feeder thread that popped from the process-wide `AudioBus`. Only one
source was in the active scene, but all four feeders still consumed the
buffer, draining speech about 4× faster than real time. The active source
received every fourth chunk, which sounded fast, choppy and partly
unintelligible; the user's saved `supertonicSpeed: 0.5` was a workaround for
that drain.

`fix: drain tts audio from only one active obs source` adds an
`AudioConsumerArbiter`: the first activated source is the only consumer,
duplicate/inactive sources emit silence, and deactivation or destruction
hands the buffer to another active source. With the other mixer inputs muted,
a speed-0.5 test message now occupies the expected ~6–8 s instead of the ~2 s
produced by four consumers. The module logs the winning source by name and a
warning for each duplicate that stays silent.

## Post-review tuning: speed and amplitude

Muse Spark 1.3 reviewed a spectrogram and waveform of the OBS capture (it
could not receive the WAV through this harness, so the review was visual;
this is stated explicitly rather than pretending it heard the audio). Its
findings matched the measurements:

- At `0.5×` the sample is unnaturally slow: ~9.5 s for a 12-word sentence,
  with ~1 s and ~1.4 s mid-utterance gaps.
- No clipping was visible in the OBS capture; peaks were tapered, not
  flat-topped.
- Recommendation: use `0.9–1.0×` for normal chat, not `0.5×`; keep OBS peaks
  around −12 to −6 dBFS and leave headroom.

Consequently the settings and dock now clamp Supertonic speed to **0.9–1.5**
(the model's documented natural range) instead of 0.5–2.0. A saved `0.5` is
migrated to `0.9` on load. The engine API still accepts 0.5–2.0 so the
packaging probe can exercise the wider range. The default remains `1.0`;
`0.9` is the slightly more deliberate chat setting. No amplitude DSP was
added: the observed capture did not clip, and hard limiting would change the
voice more than it would help.

## Method

`supertonic-probe --corpus tools/tts-bench/corpus.tsv` ran all 23 rows in one
process at 4 and 8 denoising steps, so the model was loaded once per run. The
probe synthesizes through the shipped `SupertonicEngine`, which resamples the
model's 44,100 Hz output to the worker's 22,050 Hz contract. RTF is compute
time divided by the **generated** audio duration, measured after the first
(cold, load-inclusive) row.

Host: 13th Gen Intel Core i5-13450HX, 16 threads, 15.7 GB RAM, Linux Mint
22.3. ONNX Runtime used 4 intra-op threads and 1 inter-op thread. The machine
was busy during the run (load average 5.9–8.4), so absolute times are
inflated; the 4-step vs 8-step ratio and the cross-engine comparison are the
useful signal.

## Speed

Median compute per corpus category (milliseconds) and weighted RTF:

| category | rows | 4 steps | RTF | 8 steps | RTF |
|---|---:|---:|---:|---:|---:|
| `short1` (2–3 chars) | 4 | 274 ms | 0.29 | 486 ms | 0.44 |
| `short2` (9–10 chars) | 3 | 288 ms | 0.19 | 460 ms | 0.32 |
| `short3` (15–26 chars) | 3 | 319 ms | 0.16 | 567 ms | 0.29 |
| `currency` | 2 | 522 ms | 0.15 | 988 ms | 0.29 |
| `number` | 3 | 418 ms | 0.14 | 720 ms | 0.25 |
| `repetition` | 2 | 800 ms | 0.14 | 1,452 ms | 0.25 |
| `mixed` | 3 | 423 ms | 0.14 | 1,096 ms | 0.30 |
| `unicode` | 2 | 436 ms | 0.15 | 756 ms | 0.27 |
| `long` (200 chars) | 1 | 1,924 ms | 0.13 | 3,301 ms | 0.23 |
| all | 23 | 418 ms | 0.16 | 749 ms | 0.28 |

Other measurements:

| measurement | 4 steps | 8 steps |
|---|---:|---:|
| First synthesis (includes model load) | 822–944 ms | 1,007–1,059 ms |
| Warm short message, 2–3 words | 255–328 ms | 422–584 ms |
| 200-character message, compute | 1,924 ms | 3,301 ms |
| 200-character message, generated audio | 14.35 s | 14.35 s |
| Whole corpus, 23 rows | 11.69 s | 20.85 s |
| Peak RSS (`VmHWM`) | 655.7 MB | 656.6 MB |
| Sample rate, every row | 22,050 Hz | 22,050 Hz |

The Flatpak probe measured 616–663 MB peak RSS and a 200-character M3 run at
2.31–2.73 s for 4 steps (M3 generates shorter audio than F1, so its RTF is
slightly worse). Memory is dominated by the 245 MB vector estimator plus the
other three sessions; it is stable across utterances and steps.

### Against the existing engines

| engine | 200-char compute | RTF | peak RSS |
|---|---:|---:|---:|
| Supertonic 3, F1, 4 steps | 1.92 s | 0.13 | 656 MB |
| Supertonic 3, F1, 8 steps | 3.30 s | 0.23 | 657 MB |
| Shipped Piper (LJSpeech medium, Flatpak) | 3.19–4.00 s | 0.10–0.13 | — |
| Piper VITS `en_US-kristin-medium-int8` (bake-off) | 2.36 s | 0.20 | 264 MB |
| espeak-ng 1.51 | well under real time | — | negligible |

Supertonic is **faster in wall time** than the shipped Piper voice on the
200-character phrase (1.9 s vs 3.2–4.0 s) because it generates 14.35 s of
audio where Piper generates 31.7 s for the same text. Per audio second its RTF
is slightly worse than Piper's. For live chat the wall time is what matters,
but the shorter audio also means Supertonic speaks the same sentence noticeably
faster than the Piper voice.

Short messages are the opposite story:

- Supertonic warm synthesis is ~0.26–0.33 s (4 steps) and ~0.42–0.58 s
  (8 steps), plus the model's leading silence of ~0.36–0.42 s. First audible
  speech for a one-word message is therefore ~0.6–0.8 s, and the clip carries
  ~0.5–0.7 s of trailing silence before the next message.
- Piper's shipped short-message compute in the earlier bake-off was ~0.13 s
  for 1–2 word inputs, with no comparable lead/tail padding.

So Supertonic is the better choice for long messages and a worse choice for
rapid one-word chat, unless the adapter trims the model's silence (a possible
follow-up).

## Quality spot-check

Whisper `small.en` (int8) transcribed the generated corpus. The long message,
short chat lines, punctuation, accents, repetition prompts and most numbers
were intelligible:

- `s1_hi`/`s1_gg`/`s1_lol`/`s1_no` → "Hi", "GG", "LOL", "No".
- `long_200` → the full 200-character sentence, word for word.
- `rep_hour` → "$20 an hour to $1,000 an hour."
- `uni_accent` → "déjà vu … café" (accents dropped by the ASR, pronunciation
  intact).
- `punct_dash`, `mix_punct`, `s3_question` → correct.

Failures and caveats:

- **Currency is unreliable.** `cur_250` ("Bob sent $250.00") was heard as
  "$250,000" at 4 steps and "250 dron dollars" at 8 steps; M3 8 steps heard
  "$2.50 to $1.00". `$5.00` was correct at 4 steps but not at 8. The engine
  does no number/currency expansion; the model reads the raw symbols. This
  matters for Super Chat rendering.
- **CJK is not read correctly.** `你好 chat` was heard as "Iko chat" / "Igor
  chat": the engine is pinned to the `en` language tag, so CJK characters fall
  back to English letter sounds. A language selector or detection is required
  before claiming multilingual support.
- **8 steps is not uniformly better.** `s1_lol` became "Love" at 8 steps, and
  `$5.00`/`$250.00` were worse than at 4 steps. The denoiser samples a random
  latent, so output varies; more steps reduce noise but do not guarantee a
  correct reading of symbols.
- **URLs:** the probe feeds raw text, so it spoke "https example.com". The
  plugin's default `stripUrls` removes URLs before the engine, so this is not
  the shipped behavior.
- The blind listening page includes Piper (`vits-kristin`) and three
  Supertonic variants per row; use it rather than the ASR text for voice
  quality judgments.

## Artifacts

- `build-host/tts-bench/supertonic-listen/corpus-4-steps.txt` and
  `corpus-8-steps.txt`: raw probe output.
- `build-host/tts-bench/supertonic-listen/asr.txt`: Whisper transcriptions for
  F1 4-step, F1 8-step and M3 8-step.
- `build-host/tts-bench/supertonic-listen/blind/index.html`: blind comparison
  with `map.csv`; generated from the same corpus rows.

## Reproduce

```bash
export LD_LIBRARY_PATH=/tmp/opencode/host-deps/usr/lib/x86_64-linux-gnu:/tmp/opencode/onnxruntime/lib
cmake --build build-host --target supertonic_probe
./build-host/supertonic-probe --model-dir /tmp/opencode/supertonic-assets \
  --voice F1 --steps 4 --corpus tools/tts-bench/corpus.tsv
./build-host/supertonic-probe --model-dir /tmp/opencode/supertonic-assets \
  --voice F1 --steps 8 --corpus tools/tts-bench/corpus.tsv
```

Inside the Flatpak:

```bash
./scripts/test-supertonic-flatpak.sh
```
