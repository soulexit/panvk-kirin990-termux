#!/usr/bin/env bash
# Build the Termux-native PanVK driver from the panvk-kirin990 patch series.
# Termux: pkg install git meson ninja clang patchelf zlib libdrm libc++ \
#          xorgproto libx11 libxcb libxshmfence android-shmem
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
MESA="${MESA:-$HERE/mesa}"
BASE="${BASE:-6598829019c}"
BUILD="${BUILD:-$MESA/build-termux}"

if [ ! -d "$MESA/.git" ]; then
    git clone https://github.com/funnymdzz/mesa.git "$MESA"
fi

git -C "$MESA" fetch --all
git -C "$MESA" checkout "$BASE"

for p in "$HERE"/patches/[0-9][0-9][0-9][0-9]-*.patch; do
    case "$p" in
        */0000-*) continue ;;
    esac
    echo "Applying $(basename "$p")"
    git -C "$MESA" am "$p"
done

meson setup "$BUILD" "$MESA" \
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

ninja -C "$BUILD" -j"$(nproc)"
echo "driver: $BUILD/src/panfrost/vulkan/libvulkan_panfrost.so"
