#!/data/data/com.termux/files/usr/bin/bash
# Build a Winlator/Ludashi (adrenotools) driver package from build-winlator.
set -euo pipefail

BUILD="${BUILD:-$PWD/build-winlator}"
DIST="${DIST:-$PWD/dist}"
NAME="panvk-g76-winlator"
OUT="$DIST/$NAME"
SO="$BUILD/src/panfrost/vulkan/libvulkan_panfrost.so"

if [ ! -f "$SO" ]; then
    echo "error: $SO not found; build first" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$SO" "$OUT/libvulkan_panfrost.so"

# Libraries that are not part of the Android platform and must travel with
# the driver.  libc++_shared is required by Mesa's C++ code; libz.so.1 has a
# different SONAME from the platform libz.so; libdrm is only copied when the
# link actually needs it.
needed="$(readelf -d "$OUT/libvulkan_panfrost.so" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')"
echo "NEEDED:"
echo "$needed" | sed 's/^/  /'

for lib in $needed; do
    case "$lib" in
        libc.so|libm.so|libdl.so|liblog.so|libhardware.so|libnativewindow.so|libsync.so|libandroid.so|libEGL.so|libGLESv2.so)
            ;;
        libc++_shared.so)
            cp "$PREFIX/lib/libc++_shared.so" "$OUT/"
            ;;
        libz.so.1)
            cp "$PREFIX/lib/libz.so.1" "$OUT/"
            ;;
        libdrm.so)
            cp "$PREFIX/lib/libdrm.so" "$OUT/"
            ;;
        *)
            if [ -e "$PREFIX/lib/$lib" ]; then
                echo "  bundling $lib"
                cp "$PREFIX/lib/$lib" "$OUT/"
            else
                echo "error: unresolved non-system dependency: $lib" >&2
                exit 1
            fi
            ;;
    esac
done

# Normalize bundled libraries: Termux builds carry a Termux-only RUNPATH that
# is meaningless inside Winlator.  Make everything resolve from $ORIGIN.
for lib in "$OUT"/*.so*; do
    [ "$(basename "$lib")" = "libvulkan_panfrost.so" ] && continue
    patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
done

for bad in libandroid-shmem.so libxcb.so libX11.so libX11-xcb.so libxshmfence.so; do
    if echo "$needed" | grep -qx "$bad"; then
        echo "error: driver still depends on $bad (X11/ashmem build?)" >&2
        exit 1
    fi
done

# The package must be relocatable: adrenotools loads it from an app-private
# directory and resolves the bundled C++/zlib libraries from $ORIGIN.
patchelf --set-rpath '$ORIGIN' "$OUT/libvulkan_panfrost.so"
rpath="$(patchelf --print-rpath "$OUT/libvulkan_panfrost.so")"
if [ "$rpath" != '$ORIGIN' ]; then
    echo "error: unexpected RUNPATH '$rpath'" >&2
    exit 1
fi

cat > "$OUT/meta.json" <<'JSON'
{
  "schemaVersion": 1,
  "name": "PanVK G76 (Kirin 990)",
  "description": "Experimental PanVK over Mali kbase/JM for Mali-G76 MC16. Android-only build; DXVK feature gate enabled for Winlator/Ludashi testing.",
  "author": "PanVK kbase bring-up",
  "packageVersion": "0.1.0-winlator",
  "vendor": "Mesa/PanVK",
  "driverVersion": "26.3.0-devel",
  "minApi": 28,
  "libraryName": "libvulkan_panfrost.so"
}
JSON

# Keep the unstripped library next to the package for diagnostics, but ship a
# stripped copy to keep the zip small.  (The build is release already; debug
# symbols are not generated.)
if command -v llvm-strip >/dev/null; then
    cp "$OUT/libvulkan_panfrost.so" "$DIST/$NAME.unstripped.so"
    llvm-strip --strip-unneeded "$OUT/libvulkan_panfrost.so" 2>/dev/null || true
fi

cd "$OUT"
rm -f "$DIST/$NAME.zip"
zip -r -q "$DIST/$NAME.zip" .
sha256sum "$DIST/$NAME.zip"
ls -lh "$DIST/$NAME.zip"
echo "package: $DIST/$NAME.zip"
