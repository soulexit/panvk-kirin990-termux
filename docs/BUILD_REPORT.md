# PanVK Mali-G76 (Kirin 990) — kbase JM build & smoke-test report

Date: 2026-10-20
Target repo: https://github.com/LukeValen/panvk-mali-g52
Base: `funnymdzz/mesa` @ `6598829019c0746aa8e473b4ae1c980cbfa6ea4b`
Build branch: `termux-g76` (HEAD `4637c22f5d7`); Winlator/DXVK work on `winlator-dxvk` (HEAD `0dc6991a559`)
Build type: native Termux bionic build (no cross toolchain)

## 1. Result

A working `libvulkan_panfrost.so` was built for kbase/JM and smoke-tested on
the actual device. All three milestones of the patch set reproduce:

| Test | Result |
|---|---|
| `panvk_test3` (instance / physical device / queue / device / wait idle) | PASS |
| `panvk_compute_test` (compute dispatch writes `777`) | PASS, 10/10 deterministic |
| `panvk_draw_test` (render 1 triangle, read centre pixel) | PASS, 10/10 deterministic (`255,0,0,255`) |
| `loader_test` via `VK_ICD_FILENAMES` | PASS (`Mali-G76 MC16`, API `1.3.354`) |
| `vkcube` / `vkcubepp` under Termux:X11 (XCB) | PASS, all four present modes |
| `vkmark` 1280×720, 5 scenes | PASS, score 147–154 |
| `vulkaninfo` + `vkcube` with Khronos validation | PASS, 0 validation messages |

Artifact (`termux-g76`, deployed):

```
/data/data/com.termux/files/usr/panfrost/libvulkan_panfrost.so
20,468,640 bytes
SHA-256 3db960e44c79cd52fff8fc759a0c8623ce178bd2ddad0f284f6f678f1fe09e5a
```

The tree builds clean with no compiler warnings (`ninja` → `no work to do`).
The installed binary is byte-identical to
`~/panvk-luke/build-termux/src/panfrost/vulkan/libvulkan_panfrost.so`.

## 2. Device actually used

The repo targets a Mali-G52 / kbase 11.38 / Android 14 device. This build was
validated on different hardware, which is itself part of the result:

| Item | Value |
|---|---|
| Device | Huawei P40 (ANA-NX9), Kirin 990 5G |
| GPU | Mali-G76 MC16 (arch v7) |
| Kernel | Linux 4.14.116 aarch64, Android 10 (SDK 29) |
| kbase uAPI | major 11, minor 16, **JM** (not CSF) |
| `/dev/mali0` | `crw-rw-rw-`, usable as `untrusted_app_27` |
| `/dev/dri` | absent (no panfrost/panthor DRM node) |

Consequences:

- Arch 7 is accepted on the kbase/JM backend without
  `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` in `termux-g76` and
  `winlator-dxvk`. Launchers can probe the ICD without a special env.
- The repo's CSF paths are not exercised; all submissions use the JM path.
- The uAPI is 11.16, not the expected 11.38 (see §5).

## 3. Source tree and commits

The 7 patches from the repo were applied cleanly on top of
`6598829019c`:

```
418be9b87fc apply: termux-android-detection-fixes.patch
cf0ce04576f apply: exec_init_order_fix.patch
0ca28d7054c apply: gpu_variant_fix.patch
6642cc656dd apply: android_wsi_ahb_fixes.patch
cf204b2d0b9 apply: gpu_queue_kbase.patch
d93e8684b98 apply: wsi_present_sync_fix.patch
b2c12c810e3 apply: winlator_wsi_sync_fixes.patch
```

Additional build fixes committed while bringing the port up:

```
bf3fc7330c2 build(termux): force qsort_r/memfd_create fallbacks on Android
e7cab4579fd build(termux): restrict panvk to JM archs (6,7) with kbase-only
d1ee55a39b4 build(termux): limit panvk entrypoint device-prefixes to built archs
d600172b102 build(termux): compile panthor kmod helpers for kbase-only panvk CSF references
f361767e691 kbase: bring the JM backend up on a uAPI 11.16 kernel (Kirin 990)
```

`f361767e691` contains the four working-tree fixes described in §4.

## 4. Fixes required for this kernel / device

All in `f361767e691` unless noted.

### 4.1 `pthread_{get,set}affinity_np` crash (`meson.build`)

Bionic declares these functions but only exports them from API 36. With
`-Dplatform-sdk-version=28` the compiler emits weak undefined references:
`cc.has_function()` succeeds, the symbol is NULL at runtime, and the
`u_queue` worker-thread affinity setup crashed at `u_queue.c:251`.
The feature check is now disabled on Android and Mesa uses its portable
fallback.

### 4.2 `KBASE_IOCTL_MEM_JIT_INIT` (uAPI < 11.20)

The 11.16 kernel implements the old 16-byte layout (no `phys_pages`); the
modern 24-byte ioctl number fails with `ENOTTY`. Added
`struct kbase_ioctl_mem_jit_init_11_5` and a version branch in
`kbase_kmod_dev_create()`. The JIT VA reservation is 4 GiB (panfork's
128 GiB request is rejected by this 4.19-era kernel).

### 4.3 `KBASE_IOCTL_JOB_SUBMIT` stride = 56

Despite reporting 11.16, the kernel validates atoms with the 11.19+
`base_jd_atom_v2` layout: 56 bytes, `atom_number` at offset 40,
`core_req` at 44, zeroed `renderpass_id`/padding. 48 bytes → `EINVAL`
(empirically confirmed: 40/44/48/64/72 all fail, only 56 is accepted).
`panvk_kbase_submit_and_wait()` now submits a zero-filled 56-byte atom and
logs ioctl/poll/event-code failures instead of losing them (release builds
compile out the `assert(!ret)` guards).

### 4.4 Host coherency / flaky readback (important)

Symptom: compute readback returned the initial `0xAAAAAAAA` in roughly half
the runs even though the submit event reported success (`event_code=0x1`,
`atom=1`).

Diagnosis chain:

1. A Vulkan dispatch followed by `vkCmdCopyBuffer` (GPU→GPU) read `777` from
   the result buffer in **every** run. The GPU really did write the value;
   the stale read was purely CPU-side.
2. `vkInvalidateMappedMemoryRanges()` did not help when the buffer had a
   cached mapping (`BASE_MEM_CACHED_CPU`), nor did an explicit `dc civac`.
3. A standalone raw-kbase probe (`tests/raw_coherency_test`) showed:
   - cached CPU mapping: 18/20 stale reads, `KBASE_IOCTL_MEM_SYNC`
     (`BASE_SYNCSET_OP_CSYNC`) fixed them when issued immediately;
   - non-cached CPU mapping: 0/20 stale reads;
   - `BASE_MEM_COHERENT_SYSTEM` allocations are rejected with `ENOMEM`,
     confirming the GPU is not CPU-coherent.
4. Root cause: cache maintenance by VA on this platform only affects the
   issuing CPU. A userspace thread migrating between CPU clusters leaves
   stale lines in another cluster's L1/L2, so cached host mappings are not
   reliable for either GPU→CPU readback or CPU→GPU uploads.

Fix: the kbase backend no longer advertises `PAN_KMOD_BO_FLAG_WB_MMAP` by
default. Host-visible memory is mapped non-cacheable, matching the
advertised `HOST_COHERENT` memory type, and readback no longer depends on
`vkInvalidateMappedMemoryRanges`. This made compute and draw tests
deterministic (10/10 each). `PANVK_KBASE_ENABLE_WB_MMAP=1` re-enables
cached mappings; explicit cache maintenance then goes through
`KBASE_IOCTL_MEM_SYNC` by default (`PANVK_KBASE_USER_CACHE_SYNC=1` restores
the userspace `dc` fast path).

Trade-off: CPU writes/uploads to host-visible buffers are slower (no
write-back cache). Device-local buffers used only by the GPU are
unaffected.

## 5. uAPI observations (11.16 vs the repo's 11.38)

- Kernel handshake: `11.16`, JM. The repo expects `11.38`.
- `base_jd_atom_v2` is already the 56-byte variant in practice.
- `MEM_JIT_INIT` is the pre-11.20 variant.
- The kernel has no `VK_KHR_android_surface` support in this Mesa tree:
  available WSI are `VK_KHR_surface`, `VK_KHR_xcb_surface`,
  `VK_KHR_xlib_surface`, `VK_EXT_headless_surface`,
  `VK_EXT_surface_maintenance1`. There is no on-screen Android swapchain.
- kbase sync types are CPU-emulated (`kbase_cpu_sync_type` + software
  timeline); submission is a synchronous submit-and-wait per job chain
  (`ioctl(JOB_SUBMIT)` + `poll()` + `read()`), not real GPU fences.

## 6. Build configuration

Meson setup used (native Termux clang 21.1.8, meson 1.12.1, ninja 1.13.2):

```
--buildtype=release
-Dplatforms=android,x11
-Dandroid-stub=true
-Dandroid-strict=false
-Dgallium-drivers=
-Dvulkan-drivers=panfrost
-Dpanfrost-kmds=kbase
-Dmesa-clc=auto
-Dprecomp-compiler=auto
-Dxlib-lease=disabled
-Degl=disabled -Dgles1=disabled -Dgles2=disabled
-Dopengl=false -Dglx=disabled -Dgbm=disabled
-Dlibunwind=disabled -Dzstd=disabled
-Dplatform-sdk-version=28
-Dc_args=-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
-Dc_link_args=-landroid-shmem
```

SDK 28 is deliberate: SDK ≥ 29 adds `-fno-emulated-tls`, which the device's
Bionic rejects. `-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__` allows
`AHardwareBuffer_*` (API 26) while targeting API 24.

## 7. How to reproduce the smoke tests

```sh
cd /data/data/com.termux/files/home/panvk-luke/tests

./panvk_test3
./panvk_compute_test      # expects 0x00000309
./panvk_draw_test         # expects 255,0,0,255
export VK_ICD_FILENAMES=$PWD/panvk_icd.json
./loader_test             # expects "LOADER PATH OK"

# Native Termux/X11 presentation
export DISPLAY=:0
vkcube --wsi xcb --c 600
vkmark --winsys xcb -s 1280x720 -b clear:duration=2 -b texture:duration=2
```

Diagnostic binaries left in the same directory:

- `copy_diag` — dispatch + GPU copy, proves whether the GPU wrote the value;
- `cache_diag` — readback + explicit invalidate + manual `dc civac`;
- `diag_compute` — readback sampling over 200 ms;
- `raw_coherency_test` — standalone kbase probe comparing cached/uncached
  CPU mappings, `BASE_MEM_UNCACHED_GPU` and `KBASE_IOCTL_MEM_SYNC`.

## 8. Known limitations / next steps

1. **Performance**: host-visible buffers are uncached by default (§4.4).
   Re-enable WB_MMAP only if the app pins its threads or the platform cache
   behaviour is fixed.
2. **Native X11 WSI uses MIT-SHM, not zero-copy DRI3**: the P40 exposes
   `/dev/mali0` but no `/dev/dma_heap`, no `/dev/dri`, and `/dev/ion` is not
   writable by the Termux UID. The driver therefore reports
   `sw_device=true` and copies the rendered image to shared memory.
   DRI3 raw-FD presentation would require a shareable dma-buf allocator
   (gralloc/ion/dma-heap) that this process can open.
3. **Forced Bifrost feature profile**: `geometryShader`, `tessellationShader`,
   `multiViewport`, `descriptorIndexing` and update-after-bind are advertised
   for DXVK feature-level selection. The Bifrost geometry/tessellation paths
   are not complete, so apps that actually exercise them may fail. The
   update-after-bind limits now report the real per-stage/per-set maxima
   instead of zero (`4637c22f5d7`).
4. **Synchronous submission**: each job chain blocks the submitting thread
   until the kbase event arrives. No batching, no GPU timeline fences.
5. **uAPI drift**: the backend now handles 11.16 and the 56-byte atom
   layout, but was not tested against a real 11.38 kernel.
6. Tiler heap JIT reserves 4 GiB of VA; tiler jobs work (triangle test) but
   larger workloads were not stress-tested.
7. **No direct-display WSI**: `VK_KHR_display` is compiled out because
   `pthread_cancel` is unavailable on Bionic. `vkmark -L` requests that
   extension and fails; normal XCB/XLIB/headless rendering is unaffected.
   Android ANB/AHB WSI exists only in the `winlator-dxvk` branch.

## 9. Paths

- Source: `/data/data/com.termux/files/home/panvk-luke/funnymdzz-mesa`
- Build dir: `/data/data/com.termux/files/home/panvk-luke/build-termux`
- Artifact: `build-termux/src/panfrost/vulkan/libvulkan_panfrost.so`
- Tests: `/data/data/com.termux/files/home/panvk-luke/tests`
- ICD JSON: `tests/panvk_icd.json`
- Host copies of the patch set and probes: `/root/mesa76/panvk-driver-patch`,
  `/root/mesa76/kbase-jm-prototype`, `/root/mesa76/kbase_submit_probe.c`

## 10. Winlator / Ludashi + DXVK build (2026-10-20)

An Android-only driver package for Winlator Ludashi was produced from the same
tree on branch `winlator-dxvk` (commit `0dc6991a559`, parent `f361767e691`).
Full install/test notes are in [`WINLATOR_KIRIN990.md`](WINLATOR_KIRIN990.md).

### 10.1 Build delta vs. the Termux/X11 build

- `-Dplatforms=android` (no X11/XCB, no `-landroid-shmem`).
- Link-time `-Wl,-rpath,$ORIGIN` for the Adrenotools package layout.
- `android-stub` is used at link time only; the stubs are not packaged. Runtime
  resolves Huawei's real `/system/lib64/libhardware.so` and
  `libnativewindow.so`, so `gralloc.kirin990` is used.
- Package: `dist/panvk-g76-winlator.zip`
  (SHA-256 `43bbe241be0f650b5159b67fdd56d57683eada55504ec5f9a0bc0f5887bc0502`,
  root-level `meta.json`, `libvulkan_panfrost.so`, `libc++_shared.so`,
  `libdrm.so`, `libz.so.1`).

### 10.2 Code changes in `0dc6991a559`

- Advertise `VK_KHR_android_surface` and add Android to
  `PANVK_USE_WSI_PLATFORM`, exposing `VK_KHR_surface`,
  `VK_KHR_surface_maintenance1` and `VK_KHR_swapchain` to the Android loader.
  Presentation itself is loader-implemented through
  `VK_ANDROID_native_buffer`.
- Allow arch 7 on the kbase/JM backend without
  `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` (launchers probe the ICD outside a
  container environment).
- Probe every native-handle fd with `lseek(SEEK_END)` instead of assuming the
  dma-buf is `data[0]` (Huawei gralloc may put an ashmem fd first).
- Use the 32-byte `KBASE_IOCTL_MEM_IMPORT_11_16` layout on uAPI < 11.20. This
  kernel reports 11.16 and returns `ENOTTY` for the modern 24-byte ioctl, which
  broke AHB/ANB import.
- Force the DXVK-gating Bifrost features/limits (geometry/tessellation flags,
  multi-viewport, clip/cull distance, descriptor indexing + update-after-bind,
  indirect-draw limits). These remain **testing overrides**; see §10.4.

### 10.3 Verified results (packaged, stripped driver)

All tests loaded the driver from a fresh directory containing only the zip
contents, with `LD_LIBRARY_PATH=/system/lib64`:

| Test | Result |
|---|---|
| `winlator_probe` (13 instance / 157 device extensions, DXVK gate) | PASS, 0 missing |
| `ahb_vk_test` (AHardwareBuffer import + map, 256×256 RGBA8) | PASS |
| `anb_image_test` (ANB image import, GPU clear, CPU readback) | PASS, 4096/4096 px |
| compute smoke test | PASS (`0x00000309`) |
| draw smoke test | PASS (`255,0,0,255`) |
| `unzip -t` | OK |

The ANB test is the important one for DXVK: it creates a `VkImage` from a
`VkNativeBufferANDROID` whose dma-buf is 16,384 bytes, gets a 16,384-byte /
256-byte-pitch layout, clears it on the GPU, and reads back the clear color
through `AHardwareBuffer_lock`.

One test-side finding worth recording: `VkNativeBufferANDROID.stride` is a
**pixel** stride, not bytes. Passing 256 for a 64-px-wide RGBA8 buffer made
PanVK compute a 1024-byte pitch and write past the 16 KB dma-buf, which the GPU
reported as `BASE_JD_EVENT_TERMINATED` (`event_code=0x4`) after partially
clearing. The Android loader passes the correct gralloc pixel stride; the test
was fixed to pass 64. Do not “fix” this by treating `stride` as bytes.

### 10.4 Caveats

- The forced geometry/tessellation/multi-viewport features are not complete on
  Bifrost. DXVK device creation now succeeds, but apps that exercise those
  paths may crash or render incorrectly.
- Submission stays synchronous (one submit + event wait per job chain).
- `libz.so.1` must travel with the package (Mesa zlib is enabled and Termux's
  SONAME differs from Android's `libz.so`).
- The package was validated against Huawei gralloc0 on Android 10 only.
- Next step is an actual Winlator/DXVK D3D11 sample; the driver-side gate and
  the ANB swapchain-image path are verified, but Wine/DXVK itself has not been
  run from Termux.

## 11. Native Termux X11 presentation fix (2026-10-20)

### 11.1 Root cause of `Could not find both graphics and present queues`

`log.txt` was produced by `vkcube` under Termux:X11. The active Termux ICD
(`$PREFIX/panfrost/libvulkan_panfrost.so`) was **not** the native Termux build:
it was the Android-only Winlator v4 binary
(SHA-256 `e46b53d9…`, git `0dc6991a55`). That build advertises the XCB
instance extension but was compiled without the XCB WSI entrypoints.

The loader reported:

```
ICD for selected physical device does not export
vkGetPhysicalDeviceXcbPresentationSupportKHR!
```

`vkGetPhysicalDeviceSurfaceSupportKHR()` consequently returned `false` for
queue family 0, and vkcube printed the message in `log.txt`.

Fix: deploy the native Termux build from
`~/panvk-luke/build-termux/src/panfrost/vulkan/libvulkan_panfrost.so`.
With it, the probe reports:

```
queue[0]: count=1 flags=0x7
XcbPresentationSupport(queue0)=1
SurfaceSupport(queue0) res=0 supported=1
SurfaceCaps res=0 min=3 max=0 current=64x64
```

### 11.2 Driver changes carried into `termux-g76`

`termux-g76` starts at `0dc6991a55` (arch-7 allow, uAPI 11.16 mem-import, forced
DXVK feature profile) and adds `4637c22f5d7`:

- Report the real update-after-bind descriptor limits. Before this, the forced
  `descriptorIndexing` features were paired with zero per-stage/per-set limits,
  which is spec-inconsistent and can break DXVK/VKD3D pool setup.
- Approved build recipe: the existing native `build-termux` Meson directory,
  then `ninja -C ~/panvk-luke/build-termux -j8`.

### 11.3 Validation matrix (Mali-G76 MC16, Termux:X11 XCB)

| Check | Result |
|---|---|
| `vulkaninfo --summary` without env | 1 device, Mali-G76 MC16, API 1.3.354, Mesa 26.3.0-devel |
| Instance / device extensions | 16 / 157 |
| compute smoke test | `0x00000309` |
| triangle draw + readback | `255,0,0,255` |
| `vkcube` 600 frames, 640×480 … 2560×1440 | PASS, all modes 0–3 |
| `vkcube` 1920×1080 | ≈68 fps (300 frames in 4.37 s) |
| `vkcube` 2560×1440 | ≈38 fps (300 frames in 8.0 s) |
| `vkmark` 1280×720, 5 scenes | score 147–154 |
| `vulkaninfo` + `vkcube` with Khronos validation | **0** validation messages |
| `vkmark` with Khronos validation | 27 messages, reproduced identically on llvmpipe → vkmark-side |

The `vkmark` messages are `VUID-vkQueueSubmit-pSignalSemaphores-00067`,
`VUID-vkAcquireNextImageKHR-semaphore-01779` and
`VUID-VkImageCreateInfo-initialLayout-12478`; llvmpipe reproduces all of them,
so they are not PanVK regressions.

### 11.4 Installed-driver backups

```
$PREFIX/panfrost/libvulkan_panfrost.so              current termux-g76
$PREFIX/panfrost/libvulkan_panfrost.so.0dc-force    first 0dc rebuild
$PREFIX/panfrost/libvulkan_panfrost.so.f361-termux   original native Termux build
$PREFIX/panfrost/libvulkan_panfrost.so.v4-winlator   Android-only v4 (wrong for Termux X11)
```

Restore with `cp -p <backup> $PREFIX/panfrost/libvulkan_panfrost.so`.


