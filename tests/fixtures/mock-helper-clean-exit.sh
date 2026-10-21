#!/usr/bin/env bash
# Mock helper for a successful connection that exits cleanly without emitting
# its own "ended" status; ChatClient must synthesize exactly one ended event.
set -euo pipefail

printf '%s\n' '{"type":"status","state":"connected","detail":"mock"}'
sleep 0.1
exit 0
