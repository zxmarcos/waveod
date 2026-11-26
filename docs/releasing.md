# Versions and releases

waveod follows [semantic versioning](https://semver.org): `X.Y.Z`, written `vX.Y.Z` in tags and on the pedal.

- **X**: a change that breaks what users keep: a flash format that the previous version cannot read back, an
  effect block whose knobs change meaning, the editor protocol losing a message.
- **Y**: new features, new blocks, a new protocol version that still answers the older messages.
- **Z**: fixes.

Before 1.0.0 the Y digit carries both the features and the breaking changes.

## Where the version lives

| Place | What |
|---|---|
| `VERSION` | `X.Y.Z`, the one source (`tools/version.py`) |
| `studio/pyproject.toml`, `studio/ui/package.json` | the same `X.Y.Z`, set by `tools/version.py bump` |
| the firmware (`cmake/version.cmake`) | `vX.Y.Z` on the tagged commit; `vX.Y.Z+<hash>` on any other, `.dirty` with uncommitted changes; shown in GLOBAL › SISTEMA › VERSÃO and in the protocol's info (waveod Studio) |
| tags | `vX.Y.Z`, annotated, in both repositories |
| `CHANGELOG.md` | what each version brought |

The update file's own version field (`V122`) is the bootloader's business and never changes.

## Releasing

In the private repository (the development one):

1. `python tools/version.py bump X.Y.Z`, then a `## vX.Y.Z` section in `CHANGELOG.md`.
2. Commit ("Release vX.Y.Z") and tag: `git tag -a vX.Y.Z -m "waveod vX.Y.Z"`.
3. Build the release from the tag, apart from the working tree:
   `python tools/buildfw.py out/waveod-vX.Y.Z-app.bin --rev vX.Y.Z --base blobs/QME-200_FW_USB_V1.2.2.bin`
   (the pedal shows `vX.Y.Z`), then the stock effects on their ports and waveod's own effects with them:
   `python tools/portfw.py blobs/QME-200_FW_USB_V1.2.2.bin out/waveod-vX.Y.Z.bin --elf out/waveod-vX.Y.Z-app.elf`
   (`python tools/check_ownfw.py out/waveod-vX.Y.Z.bin --elf out/waveod-vX.Y.Z-app.elf` plays each own effect in
   the emulator).
4. Check it on the pedal (install, play the factory presets, GLOBAL › SISTEMA shows `vX.Y.Z` and the bootloader
   PROTEGIDO).

Then the public repository (`D:/dev/waveod`, github.com/zxmarcos/waveod):

5. `python tools/publish.py D:/dev/waveod --ref vX.Y.Z`, review `git -C D:/dev/waveod diff --cached --stat`.
6. Commit there ("waveod vX.Y.Z" and the changelog section) and tag it the same:
   `git tag -a vX.Y.Z -m "waveod vX.Y.Z"`.
7. Push the branch and the tag, then publish the GitHub release:
   `gh release create vX.Y.Z --title "waveod vX.Y.Z" --notes-file <the changelog section>`.
   The workflow `.github/workflows/release.yml` builds the firmware of the tag on GitHub (Windows, the same
   Arm Toolchain for Embedded, checksum checked) and attaches `waveod-vX.Y.Z.bin` (the stock effects on their
   ports and waveod's own effects), `waveod-vX.Y.Z-stock.bin` (over the stock library), `waveod-vX.Y.Z.elf`,
   `waveod-studio-vX.Y.Z-windows.zip` (waveod Studio without Python: `tools/package_studio.py`, PyInstaller,
   the NAM -> Clone converter built with the static C++ runtime), `waveod-studio-vX.Y.Z-macos.zip` (the same
   built on a macOS runner: the .app for Apple silicon, the pedal's MIDI on CoreMIDI) and `SHA256SUMS.txt` (made
   by the last job over all of them). It refuses a tag that differs from `VERSION`, a build that would not show
   `vX.Y.Z`, or a converter that needs the Visual C++ runtime DLLs. To build an existing tag again: Actions ›
   Release firmware › Run workflow.

The two repositories' commits differ, but a tag names the same source in both: the public tree is the private
tag's, so a build of either shows `vX.Y.Z`.
