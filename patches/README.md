# Driver patches

This is a 23-commit series against `funnymdzz/mesa` main at
`6598829019c`.  It is the complete Kirin 990 kbase/JM bring-up and the
ARMSX2/Termux changes:

1. Termux Android detection and build fixes
2. kbase JM bring-up for uAPI 11.16
3. Android-only WSI and Winlator/DXVK feature gate
4. CPU-side present sync, Huawei linear gralloc swapchain
5. Async atoms, batched tiler+fragment submit, runtime submit thread
6. Bifrost color ROAA
7. Diagnostic no-wait present and CPU window counters

Apply with:

```bash
git checkout 6598829019c
for p in patches/[0-9][0-9][0-9][0-9]-*.patch; do
    case "$p" in *0000-cover-letter*) continue ;; esac
    git am "$p"
done
```

`0000-cover-letter.patch` is documentation only and is not applied.
The series retains the original commit messages and Mesa MIT licensing.
