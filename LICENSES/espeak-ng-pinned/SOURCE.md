# Pinned espeak-ng for libpiper

`COPYING` in this directory is the verbatim `COPYING` file from espeak-ng at
the commit that libpiper v1.7.0 builds:

- Upstream: <https://github.com/espeak-ng/espeak-ng>
- Commit: `212928b394a96e8fd2096616bfd54e17845c48f6` (2025-03-22)
- Source archive used by the Flatpak manifest:
  <https://github.com/espeak-ng/espeak-ng/archive/212928b394a96e8fd2096616bfd54e17845c48f6.tar.gz>
  (SHA-256 `1f201cabc73e569a7cb434d40d3b30980f923010f8ecd4d1c4ae94691ac2888a`)
- License: GPL-3.0-or-later

This is the copy statically linked into the bundled `libpiper.so`; it is a
different build from the espeak-ng 1.51 executable used by the fallback
engine (see `LICENSES/espeak-ng.txt`). The `COPYING` text is identical in both
versions.
