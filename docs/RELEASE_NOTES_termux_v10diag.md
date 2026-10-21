# PanVK Kirin 990 Termux v10diag — testing source/build

> **Pre-release testing build.**  Not stable.  It can crash, corrupt
> rendering, or fail to load.  Keep the stock Vulkan driver available.
> This is a diagnostic build, not a performance release.

This release contains the Termux-native PanVK driver built from the
`termux-g76` source at `10cbdd39403` (Mesa 26.3.0-devel) and the legacy
Winlator flat pack.

## Termux-native driver

- Build: `build-termux`, Android aarch64, API 28, X11 WSI enabled.
- SONAME: `libvulkan_panfrost.so`.
- SHA-256:
  `2ab681d0054b8655b5921dfd20aeda9ea85ec1afc373c5365a22c6b5c6a6dfc9`
- Dependencies include `libandroid-shmem`, `libdrm`, `libx11`/`libxcb`,
  `libxshmfence`, `libc++_shared`; install the Termux packages listed in
  the README.
- The diagnostic counters are enabled in this build.  The normal fenced
  present path is the default; no-wait present is not enabled.

Install:

```bash
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
install -m 755 libvulkan_panfrost.so "$PREFIX/lib/libvulkan_panfrost.so"
install -m 644 panfrost_icd.aarch64.json \
    "$PREFIX/share/vulkan/icd.d/panfrost_icd.aarch64.json"
export VK_ICD_FILENAMES="$PREFIX/share/vulkan/icd.d/panfrost_icd.aarch64.json"
vulkaninfo --summary
```

The build was smoke-tested with the exact-binary 4×4 draw and produced
`255,0,0,255`; the JM path reported async atom submission.  It has not
been performance-validated in a Termux game.

## Winlator / Ludashi flat pack

`panvk-g76-winlator.zip` is the older Winlator-oriented testing pack.

- `schemaVersion`: 1
- `minApi`: 28
- `libraryName`: `libvulkan_panfrost.so`
- SHA-256:
  `43bbe241be0f650b5159b67fdd56d57683eada55504ec5f9a0bc0f5887bc0502`

Do not confuse it with the ARMSX2 packs in the companion repository.

## Source

- Base: `funnymdzz/mesa` main `6598829019c`.
- Branch: `termux-g76`, HEAD `10cbdd39403`.
- Patch series: `patches/` (23 commits, MIT).
- Build report: `docs/BUILD_REPORT.md`.

## Limits

- One test device: Huawei P40, Kirin 990, Android 10, kbase 11.16 JM.
- GPU timestamps are unavailable; DVFS sysfs is not readable from
  Termux.
- The v10diag counters are for attribution; they do not make the driver
  faster.
