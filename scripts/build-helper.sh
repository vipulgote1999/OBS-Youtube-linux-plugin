#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

if ! command -v go >/dev/null 2>&1; then
    echo "error: go is not installed (Go 1.22 or newer is required)" >&2
    exit 1
fi

mkdir -p dist
(cd helper && CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -trimpath -ldflags="-s -w" -o ../dist/ytchat-helper ./cmd/ytchat-helper)

echo "== dist/ytchat-helper =="
file dist/ytchat-helper

info="$(file dist/ytchat-helper)"
if [[ "${info}" != *"x86-64"* || "${info}" != *"statically linked"* ]]; then
    echo "error: dist/ytchat-helper is not a static x86-64 ELF" >&2
    exit 1
fi
