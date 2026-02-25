# Master Progress Bar Playhead (Vertical White Line) — Smoothness Analysis & Plan

## What I found in the current implementation

1. **The UI and audio threads share `currentCyclePhase01` without synchronization.**
   - Audio thread writes `currentCyclePhase01` in `processBlock()`, then stores `lastProcessBlockWallTimeTicks` with `memory_order_release`.
   - UI thread reads `lastProcessBlockWallTimeTicks` with `memory_order_acquire`, then reads phase via `getMasterPhase()`.
   - However, `getMasterPhase()` returns a **plain `double`** (`currentCyclePhase01`) that is not atomic and not locked.
   - This is a data race in C++ and can produce occasional discontinuities/jitter in the playhead position.

2. **The code already tries to smooth motion by extrapolating between audio blocks.**
   - In both `timerCallback()` and `paintOverChildren()`, phase is extrapolated from `rawPhase + elapsedSec * bpm / (60 * cycleBeats)` using high-resolution ticks.
   - This is the correct strategy for smooth motion, but it depends on a coherent, thread-safe `rawPhase` baseline.

3. **Current visual rendering draws the playhead at subpixel X with 2 px thickness.**
   - This is generally fine and should look smooth if phase updates are coherent.
   - If the phase baseline occasionally jumps or is stale, the white line appears to “stutter,” which matches the reported behavior.

## Most likely root cause

The most likely root cause is the **unsynchronized cross-thread phase read/write** (`currentCyclePhase01`), which undermines the otherwise-correct extrapolation path.

## Plan to make the white line scroll smoothly

### Phase 1 — Fix data coherence between audio and UI threads (highest priority)

1. Convert editor-facing transport snapshot fields to atomics (or one lock-free snapshot struct):
   - `currentCyclePhase01` → `std::atomic<double> currentCyclePhase01Atomic`
   - keep `lastProcessBlockWallTimeTicks` as release/acquire synchronization anchor.
2. In `processBlock()`, write phase first, then release-store timestamp.
3. In UI reads (`timerCallback()` + `paintOverChildren()`), acquire-load timestamp and then atomic-load phase from the same snapshot model.
4. Remove/avoid plain non-atomic reads of phase on message thread.

**Outcome:** eliminates race-induced discontinuities and gives extrapolation a reliable baseline.

### Phase 2 — Ensure monotonic playhead progression on UI side (except wrap)

1. Add a lightweight guard in UI extrapolation:
   - If `newPhase` regresses unexpectedly without wrap context, clamp to previous phase for that frame.
   - Keep existing wrap detection hysteresis.
2. Keep elapsed extrapolation clamp (`0..50 ms`) to avoid huge jumps on UI stalls.

**Outcome:** masks rare scheduling anomalies and prevents visible backward snaps.

### Phase 3 — Align update path to one source of truth

1. Keep computing the displayed playhead phase in `paintOverChildren()` (closest to actual paint time).
2. Avoid using stale cached `masterPhase` for drawing the line.
3. Keep timer callback focused on state updates and repaint triggering.

**Outcome:** minimizes timing skew between compute-time and draw-time.

### Phase 4 — Visual polish (optional)

1. Test playhead thickness at 1.0 px and 1.5 px (with alpha tweak) to reduce perceived shimmer on some DPI scales.
2. Validate at 60/120/144 Hz displays.

**Outcome:** improves perceived smoothness once timing coherence is fixed.

## Validation checklist

1. Add a debug metric: frame-to-frame `abs(deltaX)` and count outlier jumps.
2. Verify no backward jumps except at wrap.
3. Run for several minutes at multiple BPM values (slow, medium, fast) and different cycle lengths.
4. Confirm consistency in standalone + plugin-host modes.

## Suggested implementation order

1. **Atomic/snapshot fix** (Phase 1)
2. **Monotonic guard** (Phase 2)
3. **One-source paint path cleanup** (Phase 3)
4. **Visual thickness tuning** (Phase 4)

This order gives the highest chance of resolving the stutter quickly while preserving current behavior.
