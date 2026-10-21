#!/usr/bin/env bash
# Mock helper for the non-fatal-error-then-exit-0 path: the retryable provider
# error must remain the final ChatClient event and must not be reported as a
# normal "ended".
set -euo pipefail

printf '%s\n' '{"type":"error","code":"network","detail":"Network error; retrying","fatal":false}'
exit 0
