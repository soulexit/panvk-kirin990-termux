# Driver tests

These sources are the validation programs used for the Kirin 990 PanVK
work.  They are deliberately small and use only Vulkan; they are not part
of the driver build.

| File | Purpose | Expected result |
|---|---|---|
| `panvk_draw_test_pack.c` | 4×4 exact-binary draw + readback | `255,0,0,255` |
| `panvk_roaa_test.c` | color attachment bound as input attachment; two full-screen draws | `255,0,255,255`, `ROAA_INPUT_ATTACHMENT_OK` |
| `panvk_draw_bench_pack.c` | 300 empty submits; exercises diagnostic counters | synthetic only |

Example build (Termux or a workstation with Vulkan headers):

```bash
cc -O2 -o panvk_draw_test panvk_draw_test_pack.c -lvulkan
cc -O2 -o panvk_roaa_test panvk_roaa_test.c -lvulkan
cc -O2 -o panvk_draw_bench panvk_draw_bench_pack.c -lvulkan
```

Set `VK_ICD_FILENAMES` to the custom ICD when running them on the phone.
The `*_pack.c` names are historical; they are normal Vulkan programs, not
packaged app code.

The synthetic bench is **not** a performance measurement.  It submits
empty `vkQueueWaitIdle` work; its submission count and atom averages are
not comparable to a game frame.  Use it only to prove that counters are
wired up.
