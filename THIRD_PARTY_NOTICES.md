# Third-Party Notices

This repository builds one Flatpak extension. Two different distribution
models apply to its third-party dependencies:

- **Bundled** components are copied into the extension image: the espeak-ng
  executable and data, and the statically linked Go chat helper.
- **Dynamically linked** components are provided by the OBS Studio Flatpak
  runtime and are not copied into the extension: Qt 6 and the OBS libraries.

Authoritative license texts for every bundled component are vendored verbatim
under [`LICENSES/`](LICENSES/) and are installed in the extension at
`share/licenses/com.obsproject.Studio.Plugin.YtChatTts/LICENSES/`. The Go
dependency texts were copied from the module cache used to build the helper;
the espeak-ng text comes from its release source archive.

## espeak-ng (bundled)

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
plugin.

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
