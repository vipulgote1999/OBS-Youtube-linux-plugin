#!/usr/bin/env bash
# Mock chat helper for ChatClient process tests.  It ignores its stdin config
# and emits one status event and two chat messages before exiting cleanly.
set -euo pipefail

printf '%s\n' '{"type":"status","state":"connected","detail":"mock"}'
sleep 0.1
printf '%s\n' '{"type":"message","message":{"id":"m1","author":"Alice","text":"hello chat","kind":"text"}}'
sleep 0.1
printf '%s\n' '{"type":"message","message":{"id":"m2","author":"Bob","text":"second message","kind":"text"}}'
