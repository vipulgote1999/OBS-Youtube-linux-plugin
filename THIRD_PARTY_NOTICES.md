# Third-Party Notices

This repository bundles third-party software inside its Flatpak extension.
The complete license text of each bundled component is reproduced verbatim
under `LICENSES/`.

## espeak-ng

- Upstream project: <https://github.com/espeak-ng/espeak-ng>
- Version: 1.51
- Source archive (as used by the Flatpak manifest):
  <https://github.com/espeak-ng/espeak-ng/archive/refs/tags/1.51.tar.gz>
  (SHA-256 `f0e028f695a8241c4fa90df7a8c8c5d68dcadbdbc91e758a97e594bbb0a3bdbf`)
- License: GPL-3.0-or-later; full text reproduced verbatim from the `COPYING`
  file of the source archive above in [`LICENSES/espeak-ng.txt`](LICENSES/espeak-ng.txt)

espeak-ng is compiled inside the Flatpak sandbox with
`--disable-shared --enable-static --without-pcaudiolib --without-sonic
--without-async` and installed as a standalone executable under
`/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/bin/espeak-ng` with
its data files under
`/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/espeak-ng-data/`.
It is invoked as a separate process at runtime and is not linked into the
plugin.
