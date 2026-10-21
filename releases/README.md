# Testing release assets

The Git repository keeps the small pack metadata and the Winlator flat
pack.  The large Termux-native `.so` is attached to the GitHub release
instead of being committed.

| Asset | SHA-256 | Notes |
|---|---|---|
| `libvulkan_panfrost-termux-v10diag.so` | `2ab681d0054b8655b5921dfd20aeda9ea85ec1afc373c5365a22c6b5c6a6dfc9` | Termux-native build at `10cbdd39403`; X11 WSI, diagnostic counters |
| `panvk-g76-winlator.zip` | `43bbe241be0f650b5159b67fdd56d57683eada55504ec5f9a0bc0f5887bc0502` | legacy Winlator/adrenotools testing pack |

Both are **pre-release testing artifacts**, not stable drivers.  See
`docs/RELEASE_NOTES_termux_v10diag.md` and the GitHub release page.
