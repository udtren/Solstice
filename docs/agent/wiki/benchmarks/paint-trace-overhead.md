---
type: benchmark
updated: 2026-10-27
sources:
  - docs/agent/paint-trace-baseline-runs.md (Trace overhead, Linux)
  - dev-tools/test-control/paint_trace_series.py (--overhead, frame_metrics, aggregate_overhead)
  - plugins/extensions/testcontrol/TestControlTiming.cpp (clockMs, processCpuTimes, TestControlFrameRecorder)
  - libs/ui/opengl/kis_opengl_canvas2.cpp (frameSwapped, the trace's command-swap hook)
related:
  - ../pitfalls/synthetic-canvas-input.md
  - ../history/gpu-phases-4.58-4.81.md
---

# Paint trace overhead

What `KRITA_PAINT_TRACE` costs in the running application, measured
2026-10-27 on Linux (Ubuntu 26.04, Xvfb software OpenGL canvas, GPU engine on
an RTX 5090 Laptop GPU) with 18 synthetic-stroke processes, three per mode
and trace state. Numbers, hashes and method are in the
[run sheet](../../paint-trace-baseline-runs.md#trace-overhead-linux-2026-10-27-test-control);
this page keeps what they mean for later measurements.

- A trace-off run has no per-input timing, so the comparison uses measures
  outside the trace: release to the last presented frame (the canvas's
  `frameSwapped`, timed by test control on the injector's clock) and the
  process CPU time. Per-input overhead within a stroke stays unmeasured.
- CPU time, wall time and the frame cadence while drawing do not change with
  the trace (below 1 %). The only consistent shift is a few milliseconds of
  stroke tail in projection-requested Build-up (+3 ms at 64 px, +6 ms at
  256 px).
- Do not subtract a correction from traced timings; for CPU and brush modes
  the overhead is below the run variation, and for projection it was seen
  only at the stroke tail, with no established cause.
- **Frame quantization.** End-of-stroke times arrive in whole frames: at
  256 px (frame interval about 32 ms on this display) they form two
  clusters one frame apart, and a median of three strokes jumps by about
  20 ms when one stroke changes cluster. Compare the stroke distributions,
  or use more strokes, before reading a difference into such a median.
- The October 5 Windows fixture check (`KisGpuStrokeTest`, no GUI) found
  -5.7 % to +7.0 % differences within run variation; this GUI series agrees
  that the trace is not a dominant cost. Windows GUI runs are still missing.
