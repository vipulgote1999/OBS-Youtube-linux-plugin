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
# flatpak-builder copies the source tree into the sandbox.  The extension is
# useless without the helper, so never start a Flatpak build without it.
"${REPO_ROOT}/scripts/build-helper.sh"

HELPER="${REPO_ROOT}/dist/ytchat-helper"
if [[ ! -f "${HELPER}" || ! -x "${HELPER}" ]]; then
    echo "error: ${HELPER} is missing or not executable after scripts/build-helper.sh" >&2
    echo "the Flatpak extension must bundle the chat helper; fix the Go helper build and retry" >&2
    exit 1
fi

"${runner[@]}" --user --install --force-clean build-flatpak "${MANIFEST}"
