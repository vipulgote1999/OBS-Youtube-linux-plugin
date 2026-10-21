#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

MANIFEST="flatpak/com.obsproject.Studio.Plugin.YtChatTts.yml"

if ! flatpak info org.freedesktop.Sdk//25.08 >/dev/null 2>&1; then
    echo "error: org.freedesktop.Sdk//25.08 is not installed" >&2
    echo "install it with: flatpak install flathub org.freedesktop.Sdk//25.08" >&2
    exit 1
fi

runner=()
if ! command -v flatpak-builder >/dev/null 2>&1; then
    if ! flatpak info org.flatpak.Builder >/dev/null 2>&1; then
        echo "error: flatpak-builder is not available" >&2
        echo "install it with: flatpak install flathub org.flatpak.Builder" >&2
        exit 1
    fi
    runner=(flatpak run org.flatpak.Builder)
fi

# CMake installs dist/ytchat-helper into the extension; build it before
# flatpak-builder copies the source tree into the sandbox.
"${REPO_ROOT}/scripts/build-helper.sh"

"${runner[@]}" --user --install --force-clean build-flatpak "${MANIFEST}"
