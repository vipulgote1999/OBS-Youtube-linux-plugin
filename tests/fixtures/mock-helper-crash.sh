#!/usr/bin/env bash
# Mock helper that connects and then dies from a signal, exercising
# QProcess::CrashExit (where exitCode alone is not meaningful).
set -euo pipefail

printf '%s\n' '{"type":"status","state":"connected","detail":"mock"}'
sleep 0.1
kill -9 "$$"
