# Tracker Engine golden fixtures

- `qbittorrent-import.txt` exercises leading, trailing, and repeated empty tiers, leading ASCII
  whitespace, whole-line comments, and multiple endpoints in one tier. The test adds trailing ASCII
  whitespace in memory so the committed fixture remains whitespace-clean.
- `canonical.txt` is the normalized qBittorrent-style text export for that input.
- `canonical.json` is the stable compact `torrentutils.tracker-list/v1` export of the same tier list.

The fixtures are UTF-8, deterministic, and contain no network-dependent data. Tests consume them
from the source tree and never contact the listed example endpoints.
