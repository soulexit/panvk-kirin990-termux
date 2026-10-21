# Termux / Winlator testing notes

These are **pre-release testing builds**.  Keep the stock Vulkan driver
available and be prepared to remove the custom ICD if the device fails to
start a Vulkan app.

## Install the raw Termux driver

```bash
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
install -m 755 build-termux/src/panfrost/vulkan/libvulkan_panfrost.so \
    "$PREFIX/lib/libvulkan_panfrost.so"
install -m 644 termux/panfrost_icd.aarch64.json \
    "$PREFIX/share/vulkan/icd.d/panfrost_icd.aarch64.json"
export VK_ICD_FILENAMES="$PREFIX/share/vulkan/icd.d/panfrost_icd.aarch64.json"
vulkaninfo --summary
```

The build enables the X11 WSI and links `libandroid-shmem`; install the
Termux packages listed in the README.  If `vulkaninfo` fails, check
`ldd "$PREFIX/lib/libvulkan_panfrost.so"` and the ICD JSON path before
changing the driver.

## Winlator / Ludashi pack

Use `releases/panvk-g76-winlator.zip` with an adrenotools-based
container.  It is a **legacy testing pack**, not the ARMSX2 pack.  The
pack metadata is:

```json
{
  "schemaVersion": 1,
  "minApi": 28,
  "libraryName": "libvulkan_panfrost.so"
}
```

The driver and its private dependencies use unique names and `$ORIGIN`
RUNPATH.  Do not rename `libvulkan_panfrost.so` inside the pack.

## ARMSX2

ARMSX2 uses a separate flat pack and a different minimum API.  See the
companion repository [`panvk-kirin990-armsx2`](https://github.com/soulexit/panvk-kirin990-armsx2).

## Logs

For ARMSX2:

```bash
scripts/collect-logs.sh
```

For Termux-native runs, keep `vulkaninfo` output and the app's stderr.
The driver's `PANVK_DEBUG`/`PANVK_JM` logs are development aids, not a
stable interface.

## What not to do

- Do not compare ARMSX2 FPS at target speed 4× with 1×.
- Do not leave the diagnostic no-wait flag enabled for normal use.
- Do not spoof MaliSX2 to obtain ARMSX2 push descriptors.
- Do not assume `AutoTune`'s `bottleneck: GPU` is confirmed; v10diag is
  collecting the CPU-side split first.
