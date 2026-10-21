# panvk-kirin990-termux

Experimental **Mesa PanVK** Vulkan driver for the Huawei P40 / Kirin 990
(**Mali-G76 MC16**) over the Android **kbase/JM** kernel interface.  This
repository contains the Termux/Winlator build recipe, the driver patch
series, validation tests, and testing releases.

> **Status: testing build, not a stable driver.**  These are development
> artifacts for one device family (Kirin 990, kbase uAPI 11.16 JM,
> Android 10/API 29).  They can crash, corrupt rendering, or fail to
> load.  Keep the stock driver available and do not use this on a device
> you cannot recover.

The current goal is to find why the ARMSX2 heavy scene remains around
12–13 fps even after the GPU submission path was made asynchronous and
the atom count was reduced.  See [FINDINGS.md](FINDINGS.md).

## Device / driver

| Item | Value |
|---|---|
| Device | Huawei P40 ANA-NX9 |
| SoC | Kirin 990 |
| GPU | Mali-G76 MC16 (Bifrost, v7) |
| Android | 10 / API 29 |
| Kernel | 4.14.116 |
| kbase uAPI | 11.16, JM, 56-byte atom stride |
| Driver identity | `Mesa 26.3.0-devel`, `VK_DRIVER_ID_MESA_PANVK` |
| Vulkan | 1.3.354 |
| Source base | `funnymdzz/mesa`, `main` @ `6598829019c` |
| Branch | `termux-g76` |
| Build | `build-termux`, Android aarch64, API 28 |

## Repository layout

```text
patches/                23-commit driver series from the upstream base
scripts/build-termux.sh reproducible Termux build
scripts/package-winlator.sh
                         flat adrenotools pack for Winlator/Ludashi
scripts/collect-logs.sh  copy driver/app logs into one directory
tests/                   Vulkan draw, ROAA and submit-bench sources
termux/                  ICD JSON and Termux environment notes
docs/BUILD_REPORT.md     detailed v5–v10diag build/validation record
releases/                testing packs (also attached to Releases)
FINDINGS.md              performance findings and open questions
```

## Build from source (Termux)

The patch series applies to `funnymdzz/mesa` main at
`6598829019c`.  The script clones the source if needed, applies the
series with `git am`, and configures the same build that produced the
tested driver.

```bash
pkg install git meson ninja clang patchelf zlib libdrm libc++ xorgproto \
    libx11 libxcb libxshmfence android-shmem vulkan-tools
git clone https://github.com/funnymdzz/mesa.git
cd mesa
git checkout 6598829019c
for p in ../patches/[0-9][0-9][0-9][0-9]-*.patch; do
    case "$p" in *0000-cover-letter*) continue ;; esac
    git am "$p"
done

meson setup build-termux \
    -Dplatforms=android,x11 \
    -Dplatform-sdk-version=28 \
    -Dvulkan-drivers=panfrost \
    -Dgallium-drivers= \
    -Dopengl=false -Degl=disabled -Dgles2=disabled \
    -Dpanfrost-kmds=kbase \
    -Dandroid-stub=true \
    -Dbuildtype=release \
    -Dc_args=-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ \
    -Dcpp_args=-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ \
    -Dc_link_args=-landroid-shmem \
    -Dcpp_link_args=-landroid-shmem
ninja -C build-termux -j8
```

Or use `scripts/build-termux.sh`, which does the same with a clean
source checkout.

## Install in Termux

The raw build is `build-termux/src/panfrost/vulkan/libvulkan_panfrost.so`.
Termux needs the X11/libandroid-shmem dependencies because the build
enables the X11 WSI.

```bash
install -m 755 build-termux/src/panfrost/vulkan/libvulkan_panfrost.so \
    "$PREFIX/lib/libvulkan_panfrost.so"
install -m 644 termux/panfrost_icd.aarch64.json \
    "$PREFIX/share/vulkan/icd.d/panfrost_icd.aarch64.json"
export VK_ICD_FILENAMES="$PREFIX/share/vulkan/icd.d/panfrost_icd.aarch64.json"
vulkaninfo --summary
```

The driver is still an experimental PanVK build.  It does not replace
the vendor driver for daily use.

## Testing releases

The current source tree is the v10diag diagnostic build.  Release assets
are marked **pre-release**:
[`v10diag-termux-testing`](https://github.com/soulexit/panvk-kirin990-termux/releases/tag/v10diag-termux-testing).

- **Termux native**: `libvulkan_panfrost-termux-v10diag.so`, SHA-256
  `2ab681d0054b8655b5921dfd20aeda9ea85ec1afc373c5365a22c6b5c6a6dfc9`.
  Built at `10cbdd39403`; smoke-tested with the exact 4×4 draw.
- **Winlator/Ludashi**: `panvk-g76-winlator.zip`, SHA-256
  `43bbe241be0f650b5159b67fdd56d57683eada55504ec5f9a0bc0f5887bc0502`.
  This is the older Winlator pack, not an ARMSX2 pack.

The Termux-native build enables X11 WSI and requires the Termux packages
listed above.  It has **not** been performance-validated in a game.  See
[docs/RELEASE_NOTES_termux_v10diag.md](docs/RELEASE_NOTES_termux_v10diag.md).

## Winlator / Ludashi (adrenotools)

The flat package in `releases/panvk-g76-winlator.zip` can be imported by
adrenotools-based containers.  It is a **legacy testing pack** made from
the Winlator-oriented build, not the ARMSX2 pack.

- `minApi`: 28
- `libraryName`: `libvulkan_panfrost.so`
- private libraries use unique SONAMEs and `$ORIGIN` RUNPATH
- SHA-256: `43bbe241be0f650b5159b67fdd56d57683eada55504ec5f9a0bc0f5887bc0502`

Use the `scripts/package-winlator.sh` script to rebuild the flat pack
from a `build-winlator` tree.  The ARMSX2-specific pack and release
assets live in the companion repository
[`panvk-kirin990-armsx2`](https://github.com/soulexit/panvk-kirin990-armsx2).

## Validation

The source tree includes:

- `tests/panvk_draw_test_pack.c` — exact-binary 4×4 draw/readback test
  (`255,0,0,255`).
- `tests/panvk_roaa_test.c` — color attachment also bound as an input
  attachment, two full-screen draws (`255,0,255,255`,
  `ROAA_INPUT_ATTACHMENT_OK`).
- `tests/panvk_draw_bench_pack.c` — 300-submit synthetic benchmark used
  to exercise the diagnostic counters.

The detailed v5–v10diag history, atom counts, and feature flags are in
[docs/BUILD_REPORT.md](docs/BUILD_REPORT.md).

## Known limits

- No root and no ADB on the test device; Termux cannot read or write the
  Mali DVFS sysfs.
- GPU timestamps are not exposed to the app (`TS=0`).
- The ARMSX2 heavy scene is still the primary benchmark; target speed
  must be the same (not 4×) for comparisons.
- This repository does not claim stable FPS improvements yet.

## Development assistance

Made with help from **DeepSeek** and **OpenCode**.

## License

The Mesa-derived patches and code are under the Mesa MIT license.  The
repository scripts and documentation are MIT.  See [LICENSE](LICENSE).
