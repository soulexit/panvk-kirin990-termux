# Findings — PanVK on Kirin 990 / Mali-G76

Last updated: 2026-10-21.  This is a development log, not a benchmark
claim.  The numbers below come from one Huawei P40 (ANA-NX9), Android 10,
kbase 11.16 JM, and must be re-measured on the same scene and target
speed before comparing.

## Summary

1. **The kbase/JM path works.**  PanVK enumerates, creates a device,
   renders, presents, and passes exact-binary draw and input-attachment
   tests on Mali-G76.
2. **Present/fence serialization is not the wall.**  Removing the
   Android present wait (diagnostic no-wait build) gave **11.85 fps**
   with EE 18 / GS 12 / GSB 12, slightly worse than the normal fenced
   path.  The no-wait switch was removed from the clean test setup.
3. **Submission overhead was reduced a lot, but FPS did not move.**
   v8 → v9 reduced `atoms/submit` from about **80.5 to 16.5–17.0** and
   the CPU fence wait from about **34 ms to 17–21 ms**.  The heavy scene
   stayed at **12–13 fps**.
4. **ROAA did what it was supposed to do, but did not raise FPS.**
   With color ROAA enabled, ARMSX2 reports
   `ROAA=yes fbfetch=yes(in-tile) texbarrier=on inpAttFB=yes` and uses
   the in-tile, driver-ordered self-read path.  The image is correct in
   the dedicated test, but the game remains at the same frame rate.
5. **The app's push-descriptor path is blocked by ARMSX2 policy.**
   `pushdesc=off` is ARMSX2's own rule for Mali drivers; it deliberately
   exempts only MaliSX2.  PanVK already advertises
   `VK_KHR_push_descriptor`.  Impersonating MaliSX2 to get push
   descriptors is not acceptable and is not done here.
6. **The next question is attribution, not another submission tweak.**
   v10diag adds CPU-side counters for draw/barrier/descriptor/present
   recording and a GPU-frequency read.  The heavy-scene run is pending.

## Device and driver

| Item | Value |
|---|---|
| Device | Huawei P40 ANA-NX9 |
| SoC / GPU | Kirin 990 / Mali-G76 MC16 (Bifrost v7) |
| Android / kernel | 10 / API 29, Linux 4.14.116 |
| kbase | uAPI 11.16, JM backend, 56-byte atom stride |
| Driver | Mesa 26.3.0-devel, PanVK identity |
| Vulkan | 1.3.354 |
| Swapchain | 2340×1080, `VK_PRESENT_MODE_MAILBOX_KHR`, 3 images |
| Game / scene | Kingdom Hearts (`SLUS-20370`), heavy scene, target speed 1× |

## Performance history

These are not apples-to-apples across runs; the scene, resolution,
shader cache, target speed and thermal state all matter.

| Build | Main change | atoms/submit | CPU wait | Heavy-scene result |
|---|---|---:|---:|---|
| v8 | async atoms, threaded submit | ≈80.5 | ≈34 ms | 12–13 fps class |
| v9 | ROAA color + in-tile self-read | ≈16.5–17.0 | ≈17–21 ms | 12–13 fps |
| v9 + no-wait present | diagnostic present change | same | present wait removed | **11.85 fps** (EE 18, GS 12, GSB 12) |

v9 session data: **15.45 fps average over 968 frames / 62.7 s**.
PerfLog snapshots in the same run ranged from **23.9 to 26.0 fps**
(`EE 18%, GS 9–11%, GSB 11–12%, VU 7–8%, GPU 0%`).  The difference
between PerfLog snapshots and the session average is the reason the
same target speed and a long window are required.

v9 atom data: `atoms_avg_x10≈165–170`, `atoms_max≈28`,
`wait_n=120`, `wait_us_avg≈17–21 ms`.  v8 data:
`atoms_avg_x10≈805`, `wait_n=240`, `wait_us_avg≈34 ms`.

## What was changed and what it proved

### kbase/JM bring-up

The stock kernel exposes the older kbase JM interface, not a DRM/Panthor
device.  The driver was brought up against uAPI 11.16, including the
56-byte atom submission layout, CPU-side semaphore resolution, and a
kbase-specific Android present path.  This is functional and is the
foundation for everything above.

### Async atoms and batched submit

v7 submits all atoms of a queue submission back-to-back with data
dependencies; v8 moves submission to the Vulkan runtime worker; v9 does
not add another submission mechanism.  The result was a large drop in
the number of separate ioctl waits and in the CPU time blocked on
fences.  It did **not** change the game's frame rate.

### Huawei gralloc / linear swapchain

Kirin gralloc defaults GPU-rendered swapchain buffers to AFBC with a
vendor address scramble.  The driver requests linear, uncompressed
buffers with `SW_READ_OFTEN|SW_WRITE_OFTEN` and the matching gralloc1
CPU flags.  Without this the display showed black frames and edge
artifacts even though rendering succeeded.

### CPU present-sync copy

kbase has no DRM fd or syncobj handles.  The Android native-buffer
present path copies the sync payloads on the CPU instead of calling
Mesa's DRM-syncobj helper.  Without it `vkQueueSignalReleaseImageANDROID`
returned `VK_ERROR_UNKNOWN` and the surface was lost.

### Bifrost color ROAA (v9)

ARMSX2 looks at `VK_EXT_rasterization_order_attachment_access`.  With
the **color** feature available it reads the live color attachment via
subpass inputs and drops the per-primitive destination-read barriers
used by the non-ROAA Mali path.  PanVK's Bifrost input-attachment
lowering already existed; v9 exposes the color feature on v7.  Depth
and stencil ROAA remain v10+ and are not advertised.

The expected banner is:

```text
ROAA=yes fbfetch=yes(in-tile) texbarrier=on inpAttFB=yes
self-read: in-pass, driver-ordered, in-tile fetch
```

Color only.  Disable with
`/sdcard/Android/data/com.armsx2.nightly/files/logs/panvk_no_roaa`
or `PANVK_ROAA=0`; the switch is read at device creation, so restart the
game.  v9's `fastShadow=NO(rt-read)` is expected for this in-tile road:
`GSFastStencilShadow::DeviceQualifies` only accepts `Copy`, measured
`InPassBarrier`, or declared loops, and the in-tile fetch does not
qualify.

### No-wait present (diagnostic only)

A file-triggered path skipped the present wait and returned an empty
fence.  It produced **11.85 fps**, i.e. no improvement and slight
regression, with visible tearing risk.  The flag was removed from the
clean test setup and the normal fenced present is the default.  This
rules out the Android present/fence path as the primary wall.

### v10diag counters

v10diag times the driver's own command-buffer entry points and writes
one extra `cpu ...` line per 120 submissions.  All values after `cpu`
are **window totals**, not per-frame averages.  It covers:

- `draw_us/draw_n` — `vkCmdDraw*`, including internal draw setup
- `barrier_us/barrier_n` — `vkCmdPipelineBarrier2`, which closes and
  reopens a JM batch
- `render_us/render_n` — dynamic-rendering begin/end
- `desc_us/desc_n` — `vkCmdBindDescriptorSets2KHR`
- `update_us/update_n` — `vkUpdateDescriptorSets`
- `vbs_us/vbs_n`, `ib_us/ib_n`, `push_us/push_n` — vertex/index/
  push-constant binds
- `present_us/present_n` — normal kbase present wait
- `gpu_khz` — Mali devfreq frequency if readable; 0 otherwise

The synthetic 300-submit bench proves the wiring:
`draw_n=1 draw_us=94 render_n=2 render_us=25`.  The heavy-scene run is
the next measurement; it is not a performance fix.

## What did not help / is not the target

- **Push descriptors:** blocked by ARMSX2's Mali rule, not a missing
  PanVK extension.  Do not spoof MaliSX2.
- **No-wait present:** measured worse; present is not the wall.
- **Driver-side swapchain resolution scaling:** ARMSX2 owns the surface
  and swapchain; not attempted.
- **GPU timestamps:** the app reports `TS=0 TS valid bits=0 TS period=0`;
  kbase sets `timestamp_frequency=0`.  They would help attribution but
  do not directly add FPS.
- **DVFS:** `PANVK_KBASE_DVFS` is opt-in, and the Mali sysfs node is not
  readable/writable from Termux.  The v10diag `gpu_khz` result will say
  whether the app can read it.

## Current hypotheses

The remaining 12–13 fps can still be any of:

1. **Driver command recording** — per-draw setup/descriptor updates/
   barriers consume most of the frame, while the GPU is underfed.
2. **GPU shader/fragment work** — Bifrost fragment shaders, overdraw,
   spills, or texture/cache pressure are the wall despite low app-side
   EE/GS percentages.
3. **Clock/DVFS** — the GPU is running at a low frequency, and the
   driver cannot change it from the app UID.

The v10diag split decides between (1) and (2)/(3).  Do not make another
optimization before that measurement.

## Reproduction notes

- Compare only at the same target speed (1×, not 4×).
- Use the same scene, resolution, shader cache state and thermal state.
- Normal fenced present is the default; the no-wait diagnostic is not
  shipped.
- The v10diag pack is a measurement build; v9 is the current ARMSX2
  testing candidate.
- Full build/validation history: [BUILD_REPORT.md](docs/BUILD_REPORT.md).
- Raw v9 ARMSX2 logs are kept in the companion repository under
  [`docs/`](https://github.com/soulexit/panvk-kirin990-armsx2/tree/main/docs).
