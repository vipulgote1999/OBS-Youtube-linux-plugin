# Third-Party Notices

This repository builds one Flatpak extension. Two different distribution
models apply to its third-party dependencies:

- **Bundled** components are copied into the extension image: ONNX Runtime,
  the Supertonic 3 model with its ten voice styles, and the statically linked
  Go chat helper.
- **Dynamically linked** components are provided by the OBS Studio Flatpak
  runtime and are not copied into the extension: Qt 6, `libobs` and
  `obs-frontend-api`.

Authoritative license texts for every bundled component are vendored verbatim
under [`LICENSES/`](LICENSES/) and are installed in the extension at
`share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/`. The Go
dependency texts were copied from the module cache used to build the helper;
the other texts were taken from the exact upstream source archives, tags or
revisions pinned in `flatpak/com.obsproject.Studio.Plugin.YtChatTts.yml`.

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
- Installed as `/app/plugins/YtChatTts/lib/libonnxruntime-ytc.so.1`
  (plus `libonnxruntime_providers_shared.so`); the CPU execution provider is
  used, with no GPU/CUDA provider and no network access.
- The SONAME is renamed from `libonnxruntime.so.1` to
  `libonnxruntime-ytc.so.1` so other OBS extensions that load a different
  ONNX Runtime ABI cannot satisfy our dependency with the wrong library.

## Supertonic 3 model and voices (bundled data)

- Upstream model: <https://huggingface.co/supertone-oss-archive/supertonic-3>
- Pinned revision:
  `aafc6e32416a594460b32413efc49d7fe4ce6d46`
- License: **BigScience Open RAIL-M**; the full text is reproduced verbatim
  from the model repository in
  [`LICENSES/supertonic-3/LICENSE`](LICENSES/supertonic-3/LICENSE), with the
  pin and installed layout recorded in
  [`LICENSES/supertonic-3/SOURCE.md`](LICENSES/supertonic-3/SOURCE.md).
- Files: four ONNX models (`duration_predictor`, `text_encoder`,
  `vector_estimator`, `vocoder`), `tts.json`, `unicode_indexer.json`, and the
  ten voice styles in `voice_styles/{F1..F5,M1..M5}.json`. The Flatpak manifest
  pins every file by SHA-256.
- Upstream inference code: <https://github.com/supertone-oss-archive/supertonic>
  (MIT). The plugin ports that archived C++ inference path; no Python or
  network service is used at runtime. The upstream MIT license and
  `Copyright (c) 2025 Supertone Inc.` notice are reproduced verbatim in
  [`LICENSES/supertonic/LICENSE`](LICENSES/supertonic/LICENSE).
- Installed under
  `/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/supertonic/`.
- Open RAIL-M use-based restrictions apply to the model and its outputs; the
  model is bundled unmodified.

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
