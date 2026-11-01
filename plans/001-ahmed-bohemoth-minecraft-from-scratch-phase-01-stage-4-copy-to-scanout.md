# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 4: Copy to scanout spike

**Status:** blocked: all agent work is done; the real dumb-buffer write is UNMEASURED and awaits Ahmed   (updated 2026-11-01)

## Goal

We know the cost of readback plus the dumb-buffer write. Pass: total of at most 2 ms.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/copy_scanout/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Time a 1080p readback from device memory to pinned host memory in `/tmp/mc_c_spikes/copy_scanout/`
- [x] Stand-in, agent-measurable: time a streaming copy into ordinary host memory and into write-combined host memory. This does not replace the next box
- [x] Write the `--dumb-buffer` path and test its gate and logic against a fake DRM shim. No real device was opened
- [ ] Time the copy from host memory into a mapped dumb buffer (**Ahmed only**: command below)
- [ ] Record both times and the total in this file (readback is recorded; the real write and the real total wait on the box above)

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    # Readback timing runs headless. The dumb-buffer part: Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 30 /tmp/mc_c_spikes/copy_scanout/copy_scanout
    pgrep -a copy_scanout    # must print nothing

The dumb-buffer run, for Ahmed on a text console. It creates and maps a buffer only: no modeset, no CRTC, no KD or VT ioctl, so the screen does not change. If nobody holds DRM master at that moment the kernel makes the opener master, and the program drops it immediately, before any other ioctl. `/tmp` is tmpfs, so if the machine has rebooted the spike is gone and this stage's measurement must be rewritten first.

    timeout -k 3 120 /tmp/mc_c_spikes/copy_scanout/copy_scanout --dumb-buffer /dev/dri/card1 | tee /tmp/copy_scanout_dumb.txt
    pgrep -a copy_scanout    # must print nothing

Decide from the `REAL dumb, spin-paced: TOTAL` row and the VERDICT line. Exit 0 pass, 1 miss, 2 refused with nothing opened, 3 error.

## Results

**The 2 ms target is not judged. The real scanout write is unmeasured and awaits Ahmed.** Agents cannot map a dumb buffer on this machine, so every "write" below went to host memory. A stand-in total under 2 ms is not a pass.

Measured 2026-11-01 23:21 to 23:25, four runs (the "does not wait" row comes from one of them), load average under 0.6, no other compute process on the GPU. One frame is 1920x1080 XRGB8888, 8,294,400 bytes. Runs after the first 10 of each phase are kept. Frame content is checked: the kernel writes a per-frame pattern and the host compares it, in full for every readback sampled (1 in 10) and at 4097 points for writes. Times are milliseconds and vary by about 0.03 between runs, except where a range is given.

| Measured | median | p99 | Rate at median |
|---|---|---|---|
| Readback to pinned, paced at 120 Hz | 1.292 | 1.30 to 1.32 | 6.4 GB/s |
| Readback to pinned, tight loop | 1.263 | 1.35 to 1.44 | 6.6 GB/s |
| Readback to unpinned, tight loop | 1.327 | 1.35 to 1.57 | 6.3 GB/s |
| Async readback: submit call only | 0.002 | 0.002 to 0.004 | n/a |
| Async readback: submit plus wait | 1.263 | 1.35 to 1.50 | 6.6 GB/s |
| Upload pinned to device (DMA) | 1.283 | 1.36 to 1.44 | 6.5 GB/s |
| glibc memcpy to ordinary host memory, warm source | 0.300 | 0.39 to 0.41 | 27 GB/s |
| NASM stream to ordinary host memory, warm source | 0.225 | 0.26 | 37 GB/s |
| NASM stream to host WC memory, warm source | 0.228 to 0.255 | 0.26 to 0.29 | 33 to 37 GB/s |

The real frame order: kernel, readback, then stream the frame just read into host WC memory, so the source is cold:

| Between frames the thread... | Readback | Write | **Stand-in total** | Total p99 |
|---|---|---|---|---|
| spins to a 120 Hz deadline | 1.264 to 1.288 | 0.53 to 0.55 | **1.79 to 1.83** | 2.00 |
| sleeps to a 120 Hz deadline | 1.28 to 1.29 | 1.31 to 1.35 | **2.59 to 2.64** | 2.65 to 3.50 |
| does not wait | 1.287 | 0.562 | 1.857 | 2.658 |

What this shows:

- **Readback alone is 1.26 to 1.29 ms**, which leaves about 0.74 ms for the write within 2 ms.
- **The host WC memory really is write-combined:** a CPU read of it runs at 102 MB/s against 11 GB/s for ordinary memory. It still sits on the CPU's memory bus, so it says nothing about a write across PCIe.
- **Sleeping between frames costs 0.8 ms in the write,** and the two write modes are clearly separate. Sleep-paced ordinary memory is even bimodal, between 0.53 and 1.9 ms. The governor is `powersave` and the core idles at 800 MHz, so waking at low frequency is the likely cause, but that was not isolated. A cold source alone explains only 0.23 to 0.53 ms.
- **Prediction, not a measurement:** if the dumb buffer lives in VRAM, the write crosses the same PCIe link. Gen3 x8 carries at most 7.88 GB/s raw, so the write takes at least 1.05 ms, and about 1.28 ms at the 6.5 GB/s measured for DMA. The total would then be about 2.3 to 2.6 ms, a **miss**. Passing needs 11.3 GB/s, above the link's limit. If the driver puts the buffer in system memory, the stand-in's 1.8 ms is the better guide. nvidia-drm's source is not on this machine, so where the buffer lives was not confirmed. Ahmed's run settles it, and its `CPU read of REAL dumb buffer` row is further evidence.

**Fallbacks to weigh if Ahmed's run misses, for Ahmed to decide:** the target can be relaxed, since the copy engine runs while the CPU does other work (the async submit call costs 2 microseconds), so readback need not block the frame thread, at the price of frame latency. Getting the GPU onto x16 (Issue 1) could roughly halve both copies, if the link is the limit. Neither was tried.

## Commits

Appended as they land:

## Issues found

**1. The GPU link is x8, not the x16 the plan assumes.**
Expected: plan section "machine" and `research/003-speed-research.md` section 5 say gen3 x16 and "roughly 12 GB/s theoretical readback".
Actual: 8.0 GT/s x8 during the copies, and 6.5 GB/s measured.
Evidence: `/sys/bus/pci/devices/0000:01:00.0/max_link_width` is 16 (the GPU's own limit), but `current_link_width` is 8, and the root port's `/sys/bus/pci/devices/0000:00:01.0/max_link_width` and `current_link_width` are both 8. The sibling port `00:01.1` is active with nothing behind it. The nvidia-smi figure the plan used, `pcie.link.width.max`, reports only the GPU's limit. `lspci` also names the GPU `TU104BM [GeForce RTX 2080 SUPER Mobile / Max-Q]`.
Done: recorded, and the master plan left verbatim. Whether firmware can give it x16 is unknown and is for Ahmed. It decides this stage: x8 cannot carry 16.6 MB per frame in 2 ms if the dumb buffer is in VRAM.

**2. Two todos cannot be finished by an agent.**
Expected: this stage times the dumb-buffer write. Actual: the hard safety rules forbid agents opening `/dev/dri/card*`, and the desktop is live.
Evidence: the stage's own Verification note, and `/sys/class/tty/tty0/active` reading `tty7`.
Done: boxes left unticked, Status set to blocked, and the run left ready for Ahmed. The default run printed `scanout write: UNMEASURED` and `VERDICT: INCOMPLETE`, and exited 0.

**3. The `--dumb-buffer` path is tested only against a fake, so it may still fail on nvidia-drm.**
Expected: the gate refuses anywhere but the visible text console, before opening anything.
Actual, under an `LD_PRELOAD` shim that stops every `/dev/dri/` open from reaching the kernel (`test_shim/run.sh`): `DISPLAY` set, `WAYLAND_DISPLAY` set, stdin not a terminal, a pty, and a console other than the visible one all exit 2 with no open attempted. With the gate passed, a denied open exits with an error; a fake dumb buffer completes at pitch 7680 and 8192; pitch 7684 is rejected and the buffer destroyed. A default run under the shim logged no `/dev/dri` open at all, from us or from libcuda.
Limit: the fake is a memfd, so those runs printed `VERDICT: PASS` and `MEASURED on a real dumb buffer`, and both lines mean nothing there. The strings are only true outside the shim. The ioctls against the real driver have never run.
Done: left as is; Ahmed's run is the first real test.

**4. Which number decides the pass was chosen here, and it matters.**
Expected: "total of at most 2 ms" has one meaning. Actual: spin-paced and sleep-paced stand-ins differ by 0.8 ms, either side of the target.
Evidence: the second Results table.
Done: VERDICT uses the spin-paced median, because a default `cudaDeviceSynchronize` busy-waits, so the real frame thread is awake when the copy starts. That reasoning is not proven by this spike, and the p99 sits right at the target. The program prints all rows so Ahmed can overrule it.
