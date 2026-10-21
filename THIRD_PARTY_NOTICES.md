# Third-Party Notices

This repository builds one Flatpak extension. Two different distribution
models apply to its third-party dependencies:

- **Bundled** components are copied into the extension image: the Piper
  `libpiper` library with its statically linked pinned espeak-ng, ONNX Runtime,
  the LJSpeech voice model and config, the espeak-ng 1.51 fallback executable
  and data, and the statically linked Go chat helper.
- **Dynamically linked** components are provided by the OBS Studio Flatpak
  runtime and are not copied into the extension: Qt 6, `libobs` and
  `obs-frontend-api`.

Authoritative license texts for every bundled component are vendored verbatim
under [`LICENSES/`](LICENSES/) and are installed in the extension at
`share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/`. The Go
dependency texts were copied from the module cache used to build the helper;
the other texts were taken from the exact upstream source archives, tags or
revisions pinned in `flatpak/com.obsproject.Studio.Plugin.YtChatTts.yml`.

## Piper / libpiper (bundled)

- Upstream project: <https://github.com/OHF-Voice/piper1-gpl>
- Version: `v1.7.0`
- Source archive (as used by the Flatpak manifest):
  <https://github.com/OHF-Voice/piper1-gpl/archive/refs/tags/v1.7.0.tar.gz>
  (SHA-256 `1fae26c92cd584671122157f3fd5d5500188906f370ad53e0f88e726ebe2e11d`)
- License: GPL-3.0-or-later; the full text is reproduced verbatim from the
  `COPYING` file of that archive in [`LICENSES/piper/COPYING`](LICENSES/piper/COPYING)
- Header-only Unicode helper: `libpiper/include/uni_algo.h` is public domain or
  MIT, at the licensor's option. Its notice block is reproduced verbatim from
  the source file in
  [`LICENSES/piper/uni_algo-NOTICE.txt`](LICENSES/piper/uni_algo-NOTICE.txt).
- Installed as the shared library
  `/app/plugins/YtChatTts/lib/libpiper.so`.
- Only the `libpiper` library is installed. The upstream `piper_exe`
  command-line tool is not shipped; the plugin calls the C API in-process and
  never spawns a Piper process. `piper.h` is installed only for the duration of
  the Flatpak build and is pruned from the final extension.

`libpiper.so` statically links the pinned espeak-ng described below. It is a
separate GPL-3.0-or-later component from this repository's GPL-2.0-or-later
code; GPL-2.0-or-later is compatible with GPL-3.0-or-later.

## ONNX Runtime (bundled)

- Upstream project: <https://github.com/microsoft/onnxruntime>
- Version: CPU build `1.22.0`, Linux x86-64 prebuilt archive
- Source archive (as used by the Flatpak manifest):
  <https://github.com/microsoft/onnxruntime/releases/download/v1.22.0/onnxruntime-linux-x64-1.22.0.tgz>
  (SHA-256 `8344d55f93d5bc5021ce342db50f62079daf39aaafb5d311a451846228be49b3`)
- License: MIT; the full text is reproduced verbatim from the archive's
  `LICENSE` file in
  [`LICENSES/onnxruntime/LICENSE`](LICENSES/onnxruntime/LICENSE)
- The archive's `ThirdPartyNotices.txt`, covering the third-party code
  compiled into the prebuilt library, is reproduced verbatim in
  [`LICENSES/onnxruntime/ThirdPartyNotices.txt`](LICENSES/onnxruntime/ThirdPartyNotices.txt).
- Installed as `/app/plugins/YtChatTts/lib/libonnxruntime.so*`; the CPU
  execution provider is used, with no GPU/CUDA provider and no network access.
- Supplied to the `libpiper` build through `ONNXRUNTIME_DIR`, so the Flatpak
  build performs no download at build time.

## LJSpeech voice model (bundled data)

- Voice: `en_US-ljspeech-medium` (US English female, single speaker, medium
  quality, 22,050 Hz)
- Upstream repository and pinned revision:
  <https://huggingface.co/rhasspy/piper-voices/tree/5041a7a49ae5b3a0f30e6837769eb03fdbb231bd/en/en_US/ljspeech/medium>
- Model file:
  <https://huggingface.co/rhasspy/piper-voices/resolve/5041a7a49ae5b3a0f30e6837769eb03fdbb231bd/en/en_US/ljspeech/medium/en_US-ljspeech-medium.onnx>
  (SHA-256 `6f52a751e2349abe7a76735eb09dc1875298c77ea2342ffd2fef79ff81b87f22`)
- Config file:
  <https://huggingface.co/rhasspy/piper-voices/resolve/5041a7a49ae5b3a0f30e6837769eb03fdbb231bd/en/en_US/ljspeech/medium/en_US-ljspeech-medium.onnx.json>
  (SHA-256 `141d612cc0a95ed7efc1ca936b845c2364967f2e9217c5dbfcf69fc4d6c65860`)
- Dataset: LJ Speech, <https://keithito.com/LJ-Speech-Dataset/>; the model card
  states the dataset license is **public domain**. The model card is reproduced
  verbatim in
  [`LICENSES/ljspeech/MODEL_CARD.md`](LICENSES/ljspeech/MODEL_CARD.md).
- Installed under
  `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/piper/`.

The `en_US-lessac-medium` voice is **not** bundled: its Lessac/Blizzard
dataset license is research-only and is not suitable for a public release.

## espeak-ng 1.51 (bundled fallback executable)

- Upstream project: <https://github.com/espeak-ng/espeak-ng>
- Version: 1.51
- Source archive (as used by the Flatpak manifest):
  <https://github.com/espeak-ng/espeak-ng/archive/refs/tags/1.51.tar.gz>
  (SHA-256 `f0e028f695a8241c4fa90df7a8c8c5d68dcadbdbc91e758a97e594bbb0a3bdbf`)
- License: GPL-3.0-or-later; full text reproduced verbatim from the `COPYING`
  file of the source archive above in [`LICENSES/espeak-ng.txt`](LICENSES/espeak-ng.txt)
- Installed as an executable, not linked into the plugin.

espeak-ng is compiled inside the Flatpak sandbox with
`--disable-shared --enable-static --without-pcaudiolib --without-sonic
--without-async` and installed as a standalone executable under
`/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/espeak-ng` with
its data files under
`/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/espeak-ng-data/`.
It is invoked as a separate process at runtime and is not linked into the
plugin. It is the fallback engine when the Piper model is missing or cannot be
initialized.

## espeak-ng pinned inside libpiper (bundled inside `libpiper.so`)

- Upstream project: <https://github.com/espeak-ng/espeak-ng>
- Commit pinned by libpiper v1.7.0:
  `212928b394a96e8fd2096616bfd54e17845c48f6` (2025-03-22)
- Source archive (as used by the Flatpak manifest, redirected to the pinned
  source tree by `flatpak/patches/libpiper-offline.patch`):
  <https://github.com/espeak-ng/espeak-ng/archive/212928b394a96e8fd2096616bfd54e17845c48f6.tar.gz>
  (SHA-256 `1f201cabc73e569a7cb434d40d3b30980f923010f8ecd4d1c4ae94691ac2888a`)
- License: GPL-3.0-or-later; the verbatim `COPYING` file from that commit is
  [`LICENSES/espeak-ng-pinned/COPYING`](LICENSES/espeak-ng-pinned/COPYING),
  with the pin/source note in
  [`LICENSES/espeak-ng-pinned/SOURCE.md`](LICENSES/espeak-ng-pinned/SOURCE.md)
- Data: libpiper requires the espeak-ng data matching this commit. It is
  installed separately from the 1.51 fallback data at
  `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/piper/espeak-ng-data/`
  so each engine keeps a matching data set.

## Qt 6 (dynamically linked, not bundled)

- Upstream project: <https://www.qt.io/>
- Components used: Qt6 Core, Widgets, Network (plugin); Qt6 Test (host tests
  only, not installed)
- License: LGPL-3.0-only, GPL-3.0-only, or a commercial license from The Qt
  Company. See <https://doc.qt.io/qt-6/licensing.html> for the authoritative
  module-by-module licensing description.
- Distribution: the plugin links dynamically against the Qt 6 libraries that
  ship in the OBS Studio Flatpak runtime (`com.obsproject.Studio`). This
  repository does not build, copy, or redistribute Qt 6, and therefore does not
  vendor a Qt license text.

## Google protobuf and gRPC (bundled inside the helper binary)

- Upstream projects:
  <https://github.com/protocolbuffers/protobuf> and
  <https://github.com/grpc/grpc-go>
- Versions: `google.golang.org/protobuf` v1.34.2 and `google.golang.org/grpc`
  v1.65.0 (pinned in `helper/go.mod`)
- Licenses: protobuf is BSD-3-Clause; gRPC-Go is Apache-2.0. The upstream
  patent grants and notice are included as well:
  [`LICENSES/go/protobuf-LICENSE`](LICENSES/go/protobuf-LICENSE),
  [`LICENSES/go/protobuf-PATENTS`](LICENSES/go/protobuf-PATENTS),
  [`LICENSES/go/grpc-LICENSE`](LICENSES/go/grpc-LICENSE),
  [`LICENSES/go/grpc-NOTICE.txt`](LICENSES/go/grpc-NOTICE.txt).
- `helper/proto/stream_list.proto` describes YouTube's published
  `V3DataLiveChatMessageService.StreamList` RPC
  (<https://developers.google.com/youtube/data/live/streaming-live-chat>). The
  generated Go bindings in `helper/proto/` are committed so a normal build does
  not need `protoc`.
- Distribution: the helper is built with `CGO_ENABLED=0` and statically links
  these Go modules, so their code is embedded in
  `dist/ytchat-helper` and in the installed
  `.../yt-chat-tts/bin/ytchat-helper`.

## Other Go helper dependencies (bundled inside the helper binary)

The helper statically links these modules as well; all are listed by
`go version -m dist/ytchat-helper` in a built binary, and their texts are
vendored under `LICENSES/go/`:

| Module | Version | License | Vendored text |
| --- | --- | --- | --- |
| `github.com/tidwall/gjson` | v1.17.1 | MIT | [`gjson-LICENSE`](LICENSES/go/gjson-LICENSE) |
| `github.com/tidwall/match` | v1.1.1 | MIT | [`match-LICENSE`](LICENSES/go/match-LICENSE) |
| `github.com/tidwall/pretty` | v1.2.0 | MIT | [`pretty-LICENSE`](LICENSES/go/pretty-LICENSE) |
| `golang.org/x/net` | v0.25.0 | BSD-3-Clause | [`x-net-LICENSE`](LICENSES/go/x-net-LICENSE), [`x-net-PATENTS`](LICENSES/go/x-net-PATENTS) |
| `golang.org/x/sys` | v0.20.0 | BSD-3-Clause | [`x-sys-LICENSE`](LICENSES/go/x-sys-LICENSE), [`x-sys-PATENTS`](LICENSES/go/x-sys-PATENTS) |
| `golang.org/x/text` | v0.15.0 | BSD-3-Clause | [`x-text-LICENSE`](LICENSES/go/x-text-LICENSE), [`x-text-PATENTS`](LICENSES/go/x-text-PATENTS) |
| `google.golang.org/genproto/googleapis/rpc` | 2024-05-28 snapshot | Apache-2.0 | [`genproto-LICENSE`](LICENSES/go/genproto-LICENSE) |

## OBS Studio libraries (dynamically linked, not bundled)

The plugin links against `libobs` and `obs-frontend-api` provided by the
`com.obsproject.Studio` runtime. OBS Studio is GPL-2.0-or-later
(<https://github.com/obsproject/obs-studio>). Those libraries are not copied
into this extension.
