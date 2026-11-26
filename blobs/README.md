# blobs

Files from Sonicake, not covered by waveod's MIT licence.

| File | What it is | SHA-256 |
|---|---|---|
| `QME-200_FW_USB_V1.2.2.bin` | The official firmware update V1.2.2 of the Sonicake Matribox II Pro (QME-200), as Sonicake distributes it | `d9ebcb8d368c0c81a2c104b1853ae1de57a5dc86ac3d5c2aff3623ce13dc64a7` |

waveod uses it as the base of its update files (`tools/mkfw.py`): the application section is replaced by
waveod's, and the effect library, the cabinet IRs, the factory presets and the drum machine's sounds and patterns
come from this file. The same file takes a pedal back to the stock firmware (see the main README).

Copyright Sonicake. It is kept here, unmodified, so that waveod builds and its checks against the original
(`tools/check_stock.py`, `tools/check_loader.py` and the others that compare with the stock code) work out of
the box.

Not here: any dump of a pedal's flash. The bootloader area holds a block tied to that unit (at 0x4000), so a
dump belongs to its owner only.
