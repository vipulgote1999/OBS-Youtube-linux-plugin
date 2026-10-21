#!/usr/bin/env bash
# Mock helper for the fatal-error-then-exit-1 path: the provider error must
# remain the final ChatClient event and must not be overwritten by
# helper_exited.
set -euo pipefail

printf '%s\n' '{"type":"error","code":"not_live","detail":"No active live stream found","fatal":true}'
exit 1
