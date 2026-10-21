#!/usr/bin/env bash
set -euo pipefail

# Stand-in for espeak-ng --stdout.  Consume the text on stdin like the real
# engine and emit the WAV selected by the test.
cat >/dev/null
exec cat "${FAKE_ESPEAK_WAV:?FAKE_ESPEAK_WAV is not set}"
