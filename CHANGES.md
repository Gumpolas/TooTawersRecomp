# Changes made to xboxrecomp 0.12.0 for The Two Towers

This lists every change made to the vendored toolkit (and the game project) while bringing the game up, oldest first. Version numbers are internal test builds.

```text
Fixes made to xboxrecomp 0.12.0 while bringing up The Two Towers
=================================================================

kernel/kernel_rtl.c, kernel/kernel_bridge.c
  RtlTryEnterCriticalSection (306) used a separate host lock table from
  Enter/Leave, so a lock taken with TryEnter and released with
  RtlLeaveCriticalSection was never released (and Leave dropped a lock it
  never took). TryEnter now shares the Enter/Leave shadow.

kernel/nv2a_pb_scan.c
  The pushbuffer walker stopped at the first JUMP. D3D's recorded
  pushbuffers jump out of the ring and back, so everything after the jump
  was lost (including InsertCallback traps). Jumps, calls and returns are now
  followed; a jump at the end of a segment (ring wrap) still ends it.

kernel/nv2a_pb_exec.c
  - SET_CONTEXT_DMA_SEMAPHORE (0x1A4), SEMAPHORE_OFFSET (0x1D6C) and
    BACK_END_WRITE_SEMAPHORE_RELEASE (0x1D70) implemented, with RAMHT/RAMIN
    lookup of the DMA object: D3D's GPU time stamp now advances.
  - D3D InsertCallback (0x1D8C fn/ctx + NOP 6/7) is queued for the CPU.

kernel/kernel_bridge.c
  The timer thread runs the queued D3D callbacks, one at a time with a short
  pause, so the CPU sees every value a callback publishes.

kernel/xbox_memory_layout.c
  - A PUT slightly below the previous one (a kick from another thread) is no
    longer treated as a ring wrap; that re-ran old commands, including old
    semaphore releases, and wound the GPU time back.
  - XBOX_LOW_AREA_BASE is overridable (the title's image is 9 MB, past the
    default low area).

kernel/kernel_memory.c, usb/ohci.c
  OHCI read descriptor addresses as virtual addresses. They are physical:
  contiguous memory is seen through 0x80000000, and ordinary memory (the VA
  returned by MmGetPhysicalAddress) is remembered per page so the two can be
  told apart. An HCCA at physical 0 is valid.

platform/mmio_decode.h
  ALU reg, [mem] forms (add/or/and/sub/xor/cmp) for trapped registers.

apu/apu_mmio_hook.c
  MOVS/STOS (with REP) against APU memory.

apu/apu_xaudio2.c
  RECOMP_AUDIO_OUT=0 skips the XAudio2 output (hosts without audio).

tools/recomp/lifter.py
  rep movs never uses memcpy when either side is in the MMIO range
  (>= 0xFE000000): memcpy's SSE moves can't be decoded by the MMIO traps.

tools/ghidra_naming/run_ghidra.sh
  Works on Linux/macOS (analyzeHeadless, python3).

--- later fixes ---

kernel/nv2a_pb_scan.c, nv2a_pb_exec.c, xbox_memory_layout.c
  The pushbuffer is fetched from a persistent GET (nv2a_pb_run) that follows
  JUMP/CALL/RETURN and stops at PUT, run synchronously from D3D's KickOff as
  well as the poller. Semaphore methods (0x1A4/0x1D6C/0x1D70) and the NOP
  callback traps (5 = idle, 6/7 = InsertCallback) are executed.

kernel/kernel_bridge.c, kernel_hal.c, kernel_rtl.c
  KEVENTs the guest builds inline are shadowed so Wait/Set/Pulse/Reset agree;
  RtlTryEnterCriticalSection shares the Enter shadow; a dispatch lock models
  the single Xbox CPU (timer DPCs take it); pushfd/popfd/cpuid helpers report a
  Pentium III with MMX/SSE (Bink picks its blitters from cpuid).

apu/apu_core.c
  The XAudio2 monitor path submits the VP/DSP output frame (it used to clear
  the buffer, so nothing the game played was audible).

tools/recomp/lifter.py, translator.py, __main__.py
  - pushfd/popfd/cpuid lifted.
  - Mid-function call/jump targets are lifted as fragments of the containing
    function (recomp_fragments.c) instead of esp-corrupting stubs.
  - Manual wrappers (extern void sub_X_gen) receive direct calls too.
  - Found by a differential test of every instruction shape in the XBE
    (lifted C vs. the same instruction run natively, tools in
    differential-test/):
      * movsx r32, bp/sp did not sign-extend.
      * rol/ror did not set CF (a following jb/jae read a stale carry).
      * cvtss2si/cvtsd2si truncated; they round to nearest like MXCSR says.
      * sqrtps/rsqrtps/rcpps only computed the low lane.
      * fld/fstp tbyte (80-bit) were no-ops; fstp tbyte did not pop.
templates/runtime/recomp_types.h
  XMM_SQRT/RSQRT/RCP lane helpers, recomp_cvt*2si, recomp_ld80/st80.

--- v9 ---
config/seed_functions.json (game)
  11 function starts that were only reached by jmp (C++ static constructor
  bodies such as the camera/cinematic manager at 0x22C30) were stubbed as
  "unresolved"; seeded so they are recompiled. Fixes the crash after the
  montage.
kernel/nv2a_sw3d.inc (new), nv2a_pb_exec.c
  Software 3D: vertex program interpreter, fixed-function composite
  transform, w-clipping, perspective-correct rasteriser, depth + stencil
  buffers, register combiners over 4 texture stages (bilinear), alpha test,
  full blend factors/equations, colour write mask; S1/S32K/CMP vertex
  formats; ARRAY_ELEMENT32; 32-bit indices.
kernel/kernel_thread.c
  KeDelayExecutionThread: timeBeginPeriod(1) and QPC-timed short delays.

--- v10 ---
apu/apu_shim.h
  Physical addresses are resolved like the OHCI model does: identity pages
  (ordinary memory) map to low RAM, everything else to the contiguous window.
  The APU used to treat all of them as low RAM, so DirectSound's voice array
  (contiguous) was written into the middle of the game's heap and the play
  cursor the game read never moved -- the "audio loops" bug.
apu/apu_vp.c
  Voices are resampled by their pitch (linear interpolation) instead of all
  being played at 48 kHz.
apu/apu_core.c, kernel/kernel_bridge.c
  The APU interrupt line reaches DirectSound's ISR (vector 5).
kernel/kernel_bridge.c
  Vblank is paced at 60 Hz on the performance counter (was ~50 Hz), timer
  thread wakes every 2 ms.
kernel/kernel_hal.c (+ several files)
  getenv results cached: the per-method diagnostic switches were a large part
  of the frame time (about 10x faster 3D in testing).
kernel/nv2a_sw3d.inc
  Stencil buffer; decoded texture cache with wrap/mirror/clamp; NV2A fog
  factor; mirror addressing.

--- v11 ---
apu/apu_core.c
  The APU frame thread runs at time-critical priority, and when it is late it
  catches up instead of dropping the time (the sound clock kept slowing on a
  busy single core; Bink paces its video by it, so intros crawled/froze).
  The log now prints "[FPS] n flips, apu m periods" once a second
  (187 periods/s = sound running in real time).
kernel/kernel_bridge.c
  Timer/vblank thread at high priority.

--- v12 ---
kernel/kernel_bridge.c
  APU interrupt delivery is now opt-in (RECOMP_APU_IRQ=1). DirectSound's DPC
  takes the DirectSound critical section; run from the timer thread while a
  game thread held it, both stalled until the dispatch-lock timeout: the
  multi-second freezes of v10/v11 on Windows. Timer thread back to normal
  priority; APU thread above normal (was time-critical).

--- v13 ---
kernel/kernel_rtl.c
  Guest critical sections hand the lock to a waiting thread on release
  (Windows critical sections are unfair; on one core Bink's background thread
  re-took its lock in a tight loop and the video decoder starved -- the EA
  logo freeze in v12 on Windows).
kernel/kernel_sync.c
  A zero-timeout wait that times out yields the CPU.

--- v14 (diagnostic) ---
kernel/kernel_rtl.c
  [CSHOT] report: samples guest critical-section enters and prints the
  hottest call site with a guest backtrace every 20M enters.

--- v15 ---
src/twotowers_manual.c (game)
  Wrapper on DirectSound's voice GetStatus (0x0012F10A). A stopped voice
  stays "playing" (flags 0x8001) until the APU interrupt reports the release
  finished, or until DirectSoundDoWork's 0.5 s timeout (0x0012DB00) forces it.
  Bink stops its sound at the end of a movie and spins on GetStatus until it
  reads 0; with the interrupt off and no DoWork on that thread it spun forever
  (the EA logo freeze on Windows, found from the v14 [CSHOT] log: main thread
  in 0x148B33 -> 0x12A092 -> 0x1293DF). The wrapper runs DSound's own timeout
  check from the poll, so a stop always completes within 0.5 s.

--- v16 (graphics) ---
kernel/nv2a_pb_exec.c, nv2a_sw3d.inc
  - CLEAR_SURFACE honours the per-channel mask. The game clears only alpha
    mid-frame (to build a light mask); clearing all four channels wiped the
    whole rendered scene -- the main reason gameplay was black.
  - Screen-space passes that the old 2D path could not draw correctly
    (stencil, reverse-subtract blending, partial colour masks, colour from
    combiner constants, depth-as-texture) now go through the full 3D pipeline.
    The shadow pass used to paint the screen dark grey.
  - Depth buffer is written back to guest memory (Z24S8) when the game samples
    it as a texture (depth-based post pass).
  - Image blit (D3D CopyRects, NV062/NV09F on subchannels 3/2) implemented:
    the heat-haze pass copies part of the back buffer; without it, black boxes.
  - Textures decoded from a surface the GPU drew into are re-decoded after
    the draw (render-to-texture was stale).
  - Combiners decoded once per batch; attribute interpolation limited to what
    the combiners read; per-row span solving. About 30% less time per frame.
  - Raster threads (bands of rows) for big batches -- only active when the
    game is not pinned to one core (ini [Compat] single_cpu=3, experimental).
  - Developer tools: RECOMP_CAPTURE=<flip>[,every] writes a frame capture,
    RECOMP_REPLAY=<file> re-renders it without the game (seconds instead of
    minutes per experiment), RECOMP_BATCHLOG, RECOMP_DIAG_BATCH, RECOMP_REPLAY_TRACE.

--- v17 ---
tools/disasm/functions.py
  _pass_imm_ref_targets no longer accepts an immediate that lands inside the
  straight-line body of a target it has just accepted. The Two Towers'
  0x0002BC40 (picks an object's material: arg or this->0x5F4) was cut at
  0x2BC47 and 0x2BC90 by two integer constants; the `je` in the second piece
  never saw the `test` in the first, so every character was drawn with a NULL
  material -- texture offset 0, format 0 -- and came out as a black
  silhouette. Characters, weapons and armour are textured now.
src/twotowers_manual.c (game)
  Diagnostic wrapper on D3DDevice_SetTexture (0x00117AD0): with TT_DEBUG=1 it
  logs textures that were never registered (Format 0) and who bound them.

--- v18 ---
kernel/kernel_hal.c
  The guest's IRQL is mirrored into the KPCR byte it reads (fs:[0x24]).
  DirectSound's lock helper (0x00128C07) skips its critical section at
  DISPATCH_LEVEL; with the byte always 0 its DPC (run from the timer thread
  under the dispatch lock) waited on a game thread that was waiting for the
  dispatch lock -- the multi-second freezes when the sound interrupt was on.
kernel/kernel_bridge.c
  APU (sound) interrupt delivery is on again by default (RECOMP_NO_APU_IRQ=1
  turns it off). Stopped voices complete through DirectSound's interrupt
  path again instead of hanging in "stopping" (sound cutting out after the EA
  logo, long freeze between the montage and the level).
src/main.c, src/twotowers_host.h (game)
  Single-CPU mode no longer pins the whole process. A TLS callback pins every
  thread to CPU 0 as it starts; the renderer's raster threads move themselves
  to the other cores, so big draw calls are split across up to 4 cores.
  [Compat] single_cpu=2 restores the old whole-process pin.
nv2a_sw3d.inc
  Raster threads take their affinity themselves; shorter idle spin.

--- v19 ---
src/twotowers_manual.c (game)
  A voice still "stopping" a second after the game started polling it is
  force-stopped (0x0012DA17); if even that does not take, its stopping/
  playing bits are cleared. v18 on Windows still had one voice polled ten
  million times while the sound went quiet. Logged as [DSOUND].
src/main.c, src/twotowers_host.h (game)
  All-threads sampling profiler, on by default for now ([Developer] profile=0
  turns it off): every 20 s the log gets [PROFT] lines per thread, which show
  where the time goes on the player's machine (movies run at ~12 fps on
  Windows but 30 in Wine).

--- v20 ---
kernel/nv2a_d3d11.inc, kernel/nv2a_d3d11_shaders.h (new)
  Direct3D 11 renderer for the NV2A, now the default. Vertex programs still
  run on the CPU; everything after that is on the graphics card: rasterising,
  depth/stencil, blending, fog, alpha test, and the register combiners (an
  uber pixel shader runs the game's combiner program from a constant buffer).
  Render targets, depth buffers and textures are cached on the GPU by guest
  address; clears, CopyRects blits and depth-as-texture reads are done there.
  The picture is presented through a DXGI swap chain on the game window.
  [Display] renderer=software (or RECOMP_RENDERER=software) goes back to the
  CPU renderer, which is also used automatically if no D3D11 device exists.
kernel/xbox_memory_layout.c
  - PUT and GET are read under the pushbuffer lock. Read before it, a poll
    that lost the race to a newer kick ran with the older PUT, "behind" the
    GPU, and the fetch walked off past the ring into stale commands -- old
    semaphore releases, which wound D3D's GPU clock back. D3D then waited
    forever for a time that would never come: the freeze on the last frame
    of a movie / on the loading screen.
  - The NV2A poll thread, display thread and sound mixer are runtime service
    threads and no longer sit on the guest's CPU 0 (xbox_UnpinServiceThread).
    D3D's vblank handler spins until that poll thread acknowledges the
    interrupt; on the shared core it waited for its own time slice to end.
src/twotowers_manual.c (game)
  - BlockOnTime (0x0011A8E0): if the GPU is idle (GET == PUT) and a time
    that was already kicked never got released, it is completed instead of
    spinning forever ([BLOCKONTIME] in the log). Safety net for the above.
  - GetStatus: a voice that stays "stopping" after DirectSound's own forced
    stop is reported to the caller as stopped (DirectSound's state is left
    alone; v19's clearing of its bits is reverted). Bink no longer hangs on
    the last frame of a movie waiting for its sound.
  - D3D vblank handler (0x0011DA80) wrapper; TT_VBFIX=1 drops the pending
    bit before the handler's acknowledge loop (experimental, off).
  - Log: [DEVSTATE] every 20 s (GPU time, PUT/GET, vblank counts).
kernel/kernel_thread.c
  Short guest sleeps use a high-resolution waitable timer (1 ms granularity
  on Windows instead of ~15 ms), for movie pacing.
apu/apu_vp.c
  RECOMP_APU_VTRACE=1 logs voice on/off/release/pause.

--- v21 ---
src/main.c (game)
  v20 froze on Windows right after the Direct3D 11 renderer showed its
  first frame. Likely cause: the all-threads sampling profiler (on by
  default) suspended every thread, graphics-driver threads included, and then
  walked the stack with the system unwinder, which takes loader locks; a
  suspended driver thread holding one stops the profiler with another thread
  still suspended. The sampler now only copies registers and stack while a
  thread is stopped (no locks) and scans the copy afterwards. It is also off
  by default again ([Developer] profile=1 turns it on).
  New hang watchdog (always on): if no frame is presented for 6 s, the log
  gets [HANG] lines -- each thread's current module+offset and the exe
  return addresses on its stack (map with v21_syms.txt). TT_NO_WATCHDOG=1
  turns it off.
kernel/nv2a_d3d11.inc
  - Present / CreateSwapChain failures are logged; if the graphics device is
    lost the game switches to the software renderer instead of going black.
  - Alt+Enter no longer lets DXGI take the window full screen.
  - Vertex and index data go into 8 MB ring buffers (MAP_NO_OVERWRITE, one
    DISCARD per wrap) instead of a DISCARD per draw. RECOMP_GPU_DISCARD=1
    restores the old behaviour.
kernel/kernel_bridge.c, src/twotowers_manual.c
  [DEVSTATE] also reports vblank and sound-interrupt counts.

--- v22 ---
src/main.c (game)
  The hang watchdog also fires before the first frame (nothing presented
  20 s after start) and when frames stop for 8 s, and it writes [DEVSTATE]
  every 20 s on its own (it used to come only from the profiler, which is
  off by default). v21 stopped on one PC before its first frame and the log
  had nothing to say about where.

--- v23 ---
kernel/xbox_memory_layout.c, src/twotowers_manual.c (game)
  Movies ran at 5-9 fps on Windows (and, it turned out, here too): the game
  paces every frame by waiting for two vblanks counted by its vblank
  callback, and only about 11 of the 60 vblanks a second reached it. The
  vblank is raised by setting PMC_INTR_0 bit 24 / PCRTC_INTR_0 bit 0, and D3D
  services it later from a DPC that checks those bits -- but the NV2A poll
  thread was zeroing all interrupt bits continuously (from before vblanks
  were raised at all), so by the time the DPC looked, the vblank was gone.
  The poll thread now leaves those two bits alone; the wrapper of D3D's
  vblank handler (0x0011DA80) clears them as the handler's write-1-to-clear
  would on hardware. All 60 vblanks a second arrive; movies play at 30 fps.
  The sound interrupt also gets through now, and the voice that stuck in
  "stopping" at the end of the first movie completes normally.
kernel/kernel_thread.c
  [SLEEPS] in the log every 20 s: the game's sleep call sites and counts.

--- v24 ---
kernel/nv2a_sw3d.inc
  Texture change detection covered only the first width*height BYTES of a
  texture -- a quarter of a 32-bit one. A movie frame that changed only
  lower down was not re-uploaded, so the GPU kept showing the old frame:
  on one PC the New Line Cinema logo stayed on screen while the montage's
  sound played on. Linear textures (movie frames, loading screens) are now
  hashed over their whole extent; swizzled ones at 4096 points across all
  of it.

--- v25 ---
kernel/nv2a_d3d11.inc
  CPU cost of the Direct3D 11 renderer cut (gameplay was CPU-bound at
  14-20 fps, the GPU at ~10%):
  - State shadowing: render targets, viewport, scissor, constant buffers,
    textures/samplers, blend/depth/raster state objects and the pipeline
    (shaders, layout, buffers) are only sent to D3D when they change;
    a frame is about a thousand draws. Clear/present invalidate the shadow.
  - Vertex/index buffers stay bound; each draw passes its ring offsets as
    StartIndexLocation/BaseVertexLocation instead of rebinding.
  - A render target's guest-memory copy is checked for CPU writes once per
    pushbuffer run instead of before every draw.
  - RECOMP_GPU_NULL=1 (developer): skip the D3D draw calls to time the CPU
    side alone.

--- v26 ---
kernel/nv2a_sw3d.inc
  The profile from a Windows PC in the battle showed the texture checksum
  (made exhaustive for linear textures in v24) as most of the main thread's
  time: a texture is bound many times a frame and was re-hashed each time.
  A texture is now checked at most once per pushbuffer run (the CPU can only
  change it between runs; GPU writes are tracked separately), and linear
  textures are sampled every 64 bytes instead of every 16.

--- v27 ---
Diagnostics for the last step to 30 fps (the battle runs at 22-28 fps on a
Windows PC with the CPU mostly idle, so frames are lost to waiting, not work):
  - [FPS] lines also count how many vblanks each frame took (1/2/3/4+).
  - [SLEEPS] lines also give the real time spent at each sleep site.

--- v28 ---
The v27 log from the battle: frames take 2 vblanks (30 fps) most of the time
and 3 in heavy moments (down to 25 fps), so the remaining cost is CPU work.
kernel/nv2a_pb_scan.c
  The method survey (counts of every GPU method seen, for the developer
  report) searched a list linearly for each method word executed -- the
  hottest loop in the main thread's profile. Now a direct index.
kernel/kernel_bridge.c
  D3D callbacks run one per timer tick instead of up to eight with a 2 ms
  Sleep between them -- that sleep was taken while holding the dispatch lock,
  and every game thread raising IRQL (DirectSound, constantly) waited on it.

--- v29 ---
The battle now holds 30 fps on the test PC (246 s logged: 30.9 flips/s
average, 6 frames that took 3 vblanks).
input/xinput_device.c, video/fb_present.c, src/twotowers_host.h (game)
  New default keyboard + mouse layout ([Input] layout=wasd):
    W/A/S/D           left stick (move); diagonals at full, not 1.41x, deflection
    left click, K, Space   A        right click, J   X
    L                 B             I                Y
    F / R             left / right trigger
    Z / C             white / black
    Enter or Esc      Start         Backspace or Tab Back
    arrow keys        d-pad         numpad 8/4/2/6   right stick
    Shift / middle click   left / right stick click
  The window now passes mouse buttons through. layout=classic keeps the old
  keys (Z/X/A/S face buttons, numpad left stick).

--- v30 ---
usb/ohci.c
  No controller input at all (pad, keyboard or mouse): the game only ever
  received ONE input report. The periodic list (where the pad's interrupt
  endpoint lives) and the control list were walked in the same tick, and each
  published its own done queue -- the control list's (the game sends rumble
  over it every frame) overwrote the periodic one, so the completion of the
  interrupt transfer was lost, the driver never queued the next one, and
  input stopped after enumeration. One done list per acknowledgement now;
  reports flow continuously. RECOMP_USB_DUMP=1 logs report counts.
input/xinput_device.c
  XInput is loaded at run time (xinput1_4, then 1_3, then 9_1_0) instead of
  being linked to xinput1_3.dll from the DirectX redistributable. Player 1 is
  the first connected XInput pad, whichever slot Windows gave it (an Xbox
  Series pad is often not slot 0); rescanned every 2 s, so a pad plugged in
  later is picked up. The log says which DLL and how many pads.
kernel/kernel_thread.c, src/twotowers_manual.c, src/twotowers_host.h (game)
  EXPERIMENTAL frame-rate modes, [Display] fps=30|60|unlocked:
  the game paces itself by waiting for 2 vblanks (gameplay) or 1 (menus)
  from its vblank callback, chosen by its flag at 0x4B75F1. fps=60 keeps that
  flag at 1-vblank pacing; fps=unlocked fills the vblank counter so the wait
  ends at once. Movies (Bink, time-based) play at normal speed at 60.
  Whether gameplay runs at normal speed depends on whether the game's
  simulation is time-based -- if it runs fast, use fps=30.
usb/usb_gamepad.c
  RECOMP_PAD_PRESS_AFTER=<ms> delays the synthetic-press probe.

--- v31 ---
input/xinput_device.c
  Keyboard/mouse did nothing on Windows while the pad worked: keys were only
  seen through the window's WM_KEYDOWN, which never arrives if the window
  does not get keyboard focus. Each key is now also read with
  GetAsyncKeyState while a window of this process is in front. The log says
  "[INPUT] keyboard/mouse input seen" on the first press, and [FBWIN] lines
  record focus/activation.
kernel/kernel_bridge.c (movie detection), src/twotowers_manual.c (game)
  fps=60 sped movies up 2x (Bink paces through the game's vblank wait). While
  a .bik file is open the game's own 30 fps pacing is kept.
kernel/kernel_bridge.c, kernel/kernel_thread.c, kernel/nv2a_pb_exec.c
  D3D callbacks the GPU reached waited for the next ~2 ms timer tick; the
  game waits on one every frame (0x000E012B). When a guest thread goes to
  sleep with a callback pending, the timer thread is woken at once (still
  >= 2 ms between two callbacks). Waking on queue instead froze loading:
  the callback then raced the D3D code still finishing the kick.
  (v32: made opt-in, RECOMP_CB_WAKE=1 -- it could still freeze a load.)
src/twotowers_host.h (game)
  An ini without [Input] layout (older build) is renamed .ini.old and
  rewritten from the current template with the user's values carried over.

--- v32 ---
kernel/kernel_file.c, kernel/kernel_bridge.c (NtClose)
  Saves "disappeared" / "Unable to load game": NtQueryDirectoryFile keeps a
  per-handle scan cursor keyed by the host HANDLE, and it was never dropped
  on NtClose. XAPI's save search stops at the first save it finds and closes
  the folder; Windows reuses the handle value for the next open, so the next
  scan of U:\ resumed at the second entry and never saw the save folder.
  The cursor is now released when the handle is closed.
kernel/kernel_bridge.c (NtFreeVirtualMemory, ordinal 199)
  Passed the addresses of the guest's 32-bit BaseAddress/RegionSize to the
  host VirtualFree as 64-bit pointers, so every call failed with
  STATUS_UNSUCCESSFUL. MEM_RELEASE now returns the block to the guest heap
  and MEM_DECOMMIT succeeds (the heap has no uncommitted pages).
kernel/kernel_bridge.c, kernel/kernel_thread.c
  The v31 "wake the timer thread for a GPU callback" change froze some loads
  in testing; it is now opt-in (RECOMP_CB_WAKE=1).
Developer aids (all off unless set):
  RECOMP_IO_TRACE=1    log every file/IO/crypto kernel call with its call
                       site, arguments, result, names opened and directory
                       entries returned.
  RECOMP_PAD_SCRIPT=f  scripted controller input; lines "<ms> <button>" or
                       "<file>+<ms> <button>" (ms after the guest first opens
                       a path containing <file>).
  RECOMP_FB_WINDOW_DUMP_EVERY=n with RECOMP_FB_DUMP=prefix  numbered dumps.

--- v33 ---
kernel/nv2a_d3d11_vp.inc (new), kernel/nv2a_d3d11.inc, kernel/nv2a_sw3d.inc
  NV2A vertex programs on the GPU. Each program is decoded once (VpIns),
  translated to an HLSL vertex shader and compiled on a background thread;
  once ready, a batch uploads only the attributes the program reads and the
  shader produces exactly what sw3d_transform did (window position * w,
  clamped D0/D1, fog, T0-T3). Programs that write constants, and batches
  whose shader is still compiling, use the CPU path. CPU cost of a replayed
  battle frame: 49 ms -> 16 ms (test machine). RECOMP_HW_VP=0 or ini
  [Display] vertex_programs=cpu turns it off; RECOMP_HW_VP_SYNC=1 compiles
  synchronously (testing). Log: [HWVP] lines every 10 s.
  CPU path: the program is decoded once per batch instead of per vertex per
  instruction (vp_run_fast; RECOMP_VP_SLOW=1 for the old interpreter), and
  big batches are transformed on worker threads (RECOMP_VTX_THREADS=n).
kernel/kernel_hal.c, kernel/nv2a_pb_exec.c, kernel/nv2a_sw3d.inc
  getenv cache is a hash table; per-method/per-batch getenv calls cached.
twotowers/postproc_apu.py (run by relift.sh / rebuild_full.sh / iterate.sh),
apu/apu_mmio_hook.c
  Constant-address APU register accesses in the generated code call the APU
  directly (xbox_apu_r32/w32) instead of page-faulting into the VEH decoder:
  DirectSound's per-voice register traffic was thousands of exceptions a
  second, under the dispatch lock (sound chugging with many enemies).
  RECOMP_APU_SITES=1 logs the accesses that still trap.
src/twotowers_manual.c, kernel/kernel_bridge.c
  Freeze in GPU flush waits (0x000E08D0 waits for its D3D callback to write
  2 to 0x1BB3BC; the per-frame callbacks write 0/1 to the same word and
  could overwrite the 2 before the waiter saw it). The 2 is held for 250 ms.
  New hook xbox_pb_callback_hook. DEVSTATE shows the flags and timer state.
kernel/kernel_thread.c
  High-resolution waits are bounded (a waitable timer that never fired left
  the timer thread -- vblank, sound IRQ, callbacks -- stopped for good).
  KeDelayExecutionThread(0) in a tight loop sleeps 0.25 ms after 200
  consecutive yields (RECOMP_SPIN_YIELD=1 = old behaviour); getenv cached.
kernel/kernel_hal.c
  [IRQLBUG] lines name the guest call site.

--- v34 ---
kernel/nv2a_d3d11.inc, video/fb_present.c, kernel/nv2a_d3d11_shaders.h
  Presentation: the swap chain follows the window's size (ResizeBuffers),
  flip model where available (RECOMP_BLT_MODEL=1 for the old one), the
  picture is fitted at its aspect with black bars (RECOMP_PRESENT_STRETCH=1
  fills), optional vsync (RECOMP_VSYNC=1). Borderless full screen toggled by
  Alt+Enter / F11 (RECOMP_FULLSCREEN=1 at start), window size
  RECOMP_WINDOW_SIZE=WxH, window title RECOMP_WINDOW_TITLE.
  Internal resolution RECOMP_RENDER_SCALE=1..8: render targets and depth
  buffers are allocated larger; viewports, scissors and surface copies are
  scaled; CPU-written pixels (movies) are scaled up on upload and read-backs
  scaled down (gpu_copy_scaled, ps_copy).
  Widescreen RECOMP_ASPECT=<w/h> with the game hook TT_ASPECT: render targets
  are also widened horizontally; a frame is shown wide only if it drew 3D
  after the game's camera was widened; 2D HUD pieces (pass-through vertex
  programs, small, not sampling a rendered frame) are drawn at 4:3
  proportions; RECOMP_HUD_STRETCH=1 stretches them instead.
  FXAA on the final picture (RECOMP_AA=fxaa, ps_fxaa).
  Mipmaps generated for swizzled textures (RECOMP_MIPMAPS=0 off) and used on
  stages whose MIN filter is a mipmap mode; anisotropic filtering
  RECOMP_ANISO=2..16.
  Texture shader dot products: DOTPRODUCT (0x11) and DOT_ST (9) stages, with
  SET_DOT_RGBMAPPING (0x1E74) and SET_SHADER_OTHER_STAGE_INPUT (0x1E78).
  Testing aids: RECOMP_PRESENT_DUMP=prefix (+ RECOMP_PRESENT_DUMP_EVERY=n),
  RECOMP_STAGE_MODES=1, RECOMP_BATCH_SHAPES=1.
src/twotowers_manual.c (game)
  0x0004E890 camera::SetPerspective wrapped: aspect * (TT_ASPECT / (4/3)),
  so the projection and the culling frustum both widen.
src/main.c (game)
  timeBeginPeriod(1) at start and Windows power throttling turned off for
  the process (EXECUTION_SPEED, IGNORE_TIMER_RESOLUTION).
kernel/kernel_thread.c
  The busy-wait sleep (v33) only engages while short sleeps really are
  short (running average of the measured sleep < 0.9 ms); on a coarse timer
  it falls back to yielding.
src/twotowers_host.h (game)
  ini template version 2 with the display options; older inis are
  rewritten keeping the user's values (old file kept as .ini.old).
input/xinput_device.c
  Alt+Enter is not Start.

--- v35 ---
video/gfx_menu.c, video/gfx_menu.h (new)
  The in-game Graphics Settings overlay:
  - Option state is read from the RECOMP_* / TT_FPS environment at start.
  - xbox_gfx_rev is bumped on every change; xbox_GfxGet / xbox_GfxAspect
    read the current values.
  - The title registers handlers: xbox_GfxSetFpsHandler and
    xbox_GfxSetSaveHandler (writes the ini).
  - The panel is drawn with GDI into a DIB at screen resolution
    (xbox_GfxMenuRender).
  - F10 arrives from the window (xbox_GfxMenuKey). Back+Start, navigation
    and closing come from player 1's controller state, keyboard stand-in
    included (xbox_GfxMenuPad).
  - While the panel is open, and until the closing button is released, the
    game sees a controller at rest.
video/fb_present.c
  - F10 is taken for the overlay (on both key-down and key-up), so Windows
    does not enter menu mode.
  - xbox_FramebufferSetFullscreen posts a message so the window thread does
    the switch; xbox_FramebufferIsFullscreen reports the current mode.
input/xinput_device.c
  xbox_InputGetState passes port 0's state through xbox_GfxMenuPad.
kernel/nv2a_d3d11.inc, nv2a_d3d11_vp.inc
  - gpu_draw_menu draws the panel, alpha-blended and centred, before Present.
  - gpu_apply_settings runs after Present when xbox_gfx_rev moves:
    - FXAA, vsync, stretch and vertex-program mode are runtime flags.
    - Samplers are rebuilt for an anisotropy change. Textures are rebuilt
      when mipmaps are switched on or off.
    - An internal resolution or aspect change frees every surface and
      recomputes g_rsx/g_rsy.
  - The HWVP compile thread is always set up, so the GPU vertex-program path
    can be switched on in game (g_hwvp_user).
  - Only frame-sized targets (>= 320x200, GS_SCALED) are scaled.
    Coordinates use each surface's own scale (SX/SY), and blits between
    surfaces of different scales go through vs_blit.
kernel/kernel_bridge.c, kernel/kernel_thread.c
  - The timer thread runs on a fixed 2 ms grid with xbox_precise_wait_until.
    That function learns the host sleep overshoot, sleeps short, then
    yields until the deadline.
  - The timer thread is time-critical and unpinned.
  - RECOMP_TIMER_SLEEP=1 restores the plain sleep.
kernel/kernel_rtl.c, kernel/kernel_hal.c
  - Critical-section shadows use a per-thread 32-entry cache in front of the
    SRW-locked table.
  - The statistics counters are per thread and batched, with no
    interlocked add per call.
src/twotowers_manual.c (game)
  - The camera hook reads the live aspect (xbox_GfxAspect).
  - The fps delay hook is always installed (no-op at 30), and tt_fps_set
    switches the mode at run time.
src/twotowers_host.h (game)
  Overlay changes are saved to [Display] in the ini.
src/setup_launcher.cpp (game)
  - "Graphics settings..." dialog with one drop-down per [Display] option;
    custom ini values are kept.
  - Started with --graphics, the launcher opens the dialog directly.

--- v36 ---
config/seed_functions.json (game)
  The upgrade screen's switch arms (jump tables 0x6EF40, 0x6EF50 and
  0x6EF60: 0x6EA84/8B/92/99, 0x6EC40/47/4E/55, 0x6ECCA/D1/D8/DF) are seeded as
  entries.
  - One copy of the screen's update chunk (sub_0006E9EC) has its indexed jmp
    unfolded: RECOMP_ITAIL to a mid-function address, which nothing resolved.
  - The game logged "[ICALL] Failed to resolve VA 0x0006EA99" and the screen
    went black.
  - Full pipeline re-run (disasm -> ... -> lift).
input/input_bind.c, input/input_bind.h (new)
  - 24 actions: the controller's buttons, plus each stick direction as a
    keyboard action. Each action has up to 3 keys or mouse buttons, and
    button actions have a pad source (pad remap with swap).
  - Key names round-trip through the ini (kb_<action>, pad_<action>).
  - The defaults are the old wasd and classic layouts.
  - xbox_BindSet / xbox_BindAssignKey / xbox_BindAssignPad /
    xbox_BindResetAll, with a save handler.
input/xinput_device.c
  - keyboard_state reads the bindings.
  - XInput buttons are translated through the pad bindings (triggers stay
    analog).
  - xbox_InputKeyDown is exported for the overlay's key capture.
  - Pad script: anchors work when the file opens before the first input
    poll; "CLEAR" stops untimed lines; "FLAGn" raises a named flag for test
    hooks (xbox_PadScriptFlag).
video/gfx_menu.c
  - A Controls page alongside Graphics (row 0 picks the page).
  - Key capture polls every virtual key: Esc cancels, Delete clears, F10
    leaves.
  - Left/Right cycles the pad source; "Reset all controls" is the last row.
kernel/nv2a_d3d11_ps.inc (new), nv2a_d3d11.inc, nv2a_d3d11_vp.inc
  - Specialized pixel shaders. The batch's combiner program (stages,
    register inputs/outputs, mappings, ops, mux, final combiner, texture
    stage modes/flags, dot-product mapping, fog mode, alpha-test function)
    is the key.
  - It is translated to straight-line HLSL that uses the same cbuffer, and
    compiled on the vertex-program thread. ps_main draws until it is ready.
  - Replays are bit-identical to ps_main (captures 0-5).
  - RECOMP_HW_PS=0 off, RECOMP_HW_PS_SYNC=1 compile at once.
  - [HWPS] statistics appear in the [FPS] block and at the end of a replay.
  - g_sh.cbps was 1120 bytes, but the PS constants have been 1152 bytes since
    v34 (sdot, sinp): the shadow copy overran into the following fields.
src/twotowers_manual.c (game)
  - 0x000E3AF0 (the developer switch lookup, empty on retail) can answer
    TT_TWEAKS="SKIPMAINMENU=1;HERO=aragorn;LEVEL=02GATE2": straight into a
    level, for testing.
  - TT_DEBUG_GOTO=<1-11>: once a pad script raises FLAG1, the front end
    switches to that screen (10 = end of level, 11 = upgrades). It runs from
    the main loop's 0x000E6080 call. With neither variable set, nothing
    changes.
src/twotowers_host.h (game)
  [Controls] is loaded into the bindings at start, and changes made in game
  are written back.
src/setup_launcher.cpp (game)
  - "Controls..." dialog: key boxes capture the next key or mouse button,
    pad drop-downs swap, "x" clears, and Defaults restores.
  - --controls opens it directly.

-- 1.1 ---------------------------------------------------------------------
kernel/nv2a_d3d11.inc
  - Shadows: the character shadow pass draws a 256x256 colour target, then
    uses the same memory as the zeta of the next pass and samples it as a
    texture (the depth it just wrote, read as A8R8G8B8). Colour and zeta
    surfaces at one address were separate D3D textures and the colour one
    was always sampled: shadows came out as stripes. A texture now samples
    whichever surface at its address was drawn into last (GSurf.wr).
  - RECOMP_SURF_LOG=1 (testing): render targets as they are created, and
    textures that are, or lie inside, one.
  - Picture adjustments on the way to the window: brightness (gamma),
    contrast-adaptive sharpening, colour saturation (cbuffer b3 of the
    present and FXAA shaders).
kernel/nv2a_d3d11_shaders.h
  post_sharp / post_grade in ps_present and ps_fxaa.
kernel/nv2a_sw3d.inc
  - xbox_tex_observer: the title's hook on every decoded texture, with its
    content hash (may change the texels).
  - Texture cache entries are re-decoded when xbox_tex_rev changes (texture
    mods switched on or off).
video/gfx_menu.c, gfx_menu.h
  Brightness, Sharpening, Colour and the two mod switches; xbox_GfxGetF.
video/fb_present.c
  - The mouse is captured while a button is held, so a release outside the
    window still arrives; WM_CAPTURECHANGED and WM_ACTIVATEAPP let go of
    held buttons and keys.
  - xbox_FramebufferKeyClear.
input/xinput_device.c
  - A key the window holds but the keyboard reports up for 250 ms is let
    go (once GetAsyncKeyState has been seen working). A lost release left W
    held: the hero ran on his own and the controller could not steer.
  - A pushed controller stick always wins over the keyboard's.
  - Pad scripts: LSU/LSD/LSL/LSR (left stick, testing).
  - xbox_pad_observer gets the port and sees every port.
src/main.c (game)
  - Only threads that start in the program are pinned to CPU 0. Library
    threads (XAudio2's mixer, the graphics driver's, XInput's) had to share
    CPU 0 with the game: at 60 fps / unlocked the sound crackled and the
    music dropped out.
  - abort() and unhandled exceptions are logged ([CRASH]).
src/e3hud.c, e3hud_*.inc, e3hud_layout.h (game, new)
  The ECTS 2002 demo HUD and orange health pickups (see README, Mods).
src/twotowers_manual.c (game)
  - 0x000CF450 (the HUD's draw) wrapped: the ECTS HUD hides the retail gauge
    frame, moves the rating text and lends its own text elements a free slot
    of the HUD list, so the game draws them in its own fonts.
  - 0x0008FB60 ("does the player need a power-up") wrapped:
    TT_FORCE_PICKUPS=1 (testing).
  - TT_POKE8="addr:value,..." (testing) holds guest bytes, e.g. the debug
    menu switch 0x1B0BFD "DEBUG/Drop Pickups".
src/twotowers_host.h (game)
  [Display] brightness / sharpening / colour, [Mods], [Developer]
  texture_dump.
input/xinput_device.c, video/fb_present.c (1.1, later)
  - Controller, keyboard and mouse reach the game only while its window is
    in front ([Input] background_input=1 lets the controller through).
  - The click that brings the window to the front is not passed on as the
    left mouse button's game button.
1.1, playtest fixes
  - The mods are renamed for where the footage is from: "ECTS 2002 HUD
    (experimental)" and "Orange health pickups (shown in pre-release &
    making-of videos)". Settings keys unchanged (e3_hud, e3_pickups).
    README explains what each mod changes and why.
  - Launcher: labels too long for the settings window wrap onto two lines
    instead of being cut off.
  - Orange health pickups: recolour hue moved from amber-gold to orange.
  - Orange health pickups: the health vial's texture (0x71C950C7, found from a
    playtest video) is built in; only its glass panes (x < 46) are
    recoloured, the coin beside them is left alone.
  - Level-select map at 16:9: the camera is widened only in gameplay
    (front-end screens 6 and 8). The map's 3D scene was widened while its
    2D highlights stayed 4:3, so the glows sat beside the level icons.
  - texture_dump=1 works in any section of the ini; the log names the
    folder. The raw (address-named) dumps went beside the program as
    texture_dumpNN_... files; they now go inside the folder too.
  - Diagnostics for silent exits: a closed game window logs [EXIT]; game
    threads keep 64 KB of stack back so a stack overflow still logs
    [CRASH]. (A movie freeze seen once on Amon Hen ended its log with
    neither; it did not reproduce here.)
src/e3hud_load.inc (game, new)
  The loading-screen tip about experience (texture 0x509D0703), redrawn in
  the ECTS style, with a second arrow and lines on the combo runes.

-- 1.1.1 -------------------------------------------------------------------
Found by a scripted run through every level, hero and frame-rate mode.

kernel/kernel_bridge.c
  - KeInsertQueueDpc is now thread-safe. Interrupt handlers queue DPCs
    from the timer thread while game threads queue their own; two at once
    could take the same queue slot and one DPC was lost. When it was D3D's
    (its interrupt handler masks the GPU interrupt and its DPC unmasks it),
    vblank was never delivered again and the game froze on its next frame
    wait -- the freezes during movies (Amon Hen, the prologue's opening).
    The prologue run went from 64 freeze reports to none.
  - A DPC already queued is not queued twice (KDPC.Inserted), as on the
    console.
  - Safety net: if the GPU interrupt stays masked for 2 s with nothing
    queued, it is unmasked as the DPC would have, and the log says so.
kernel/nv2a_d3d11.inc
  - A colour surface that has to grow keeps its contents. The lava's heat
    haze copies two patches of the frame into one buffer; the second,
    taller patch re-created the buffer and cleared the first: black boxes
    over the lava. A frame-pitch copy target is also made frame sized at
    once.
kernel/kernel_file.c
  - Read-only opens share reading and writing, and a briefly locked file
    is retried for up to half a second. Another program reading a game
    file at the same moment (an antivirus scan, a backup) made the open
    fail, which the game reports as a dirty disc.
input/xinput_device.c
  - Controller hot-plug scanning moved to its own thread. Polling empty
    XInput slots is slow on many PCs and ran on the game's thread every
    two seconds: a small regular hitch.
  - Stick dead zone, [Input] stick_deadzone (percent, default 10, 0 = off):
    a resting stick that drifts no longer walks the hero; the range is
    rescaled so full speed is still reached.
  - Rumble is sent only when it changes, stops while the game is in the
    background (resumes in front), and stops when the window closes.
src/twotowers_manual.c (game)
  - The GPU flush wait's "2" (0x1BB3BC) is now held until the game has
    presented two more frames, as well as for 250 ms. On a busy PC a sleep
    of the waiting thread could outlast the 250 ms; the next frame's
    callbacks then overwrote the 2 before it was seen and the game waited
    for ever -- a freeze while loading (seen loading Weathertop).
video/fb_present.c, src/main.c
  - A closed game window logs [EXIT]; game threads keep 64 KB of stack for
    the crash handler, so a stack overflow still logs [CRASH].

-- 1.1.2 -------------------------------------------------------------------
Found from a player's log: the game slowed down, lost input or crashed
about eight minutes into a session (it showed after an Amon Hen cutscene).

kernel/kernel_bridge.c
  - The kernel call counter was a signed 32-bit int. The game makes about
    four million kernel calls a second (one thread sleeps for zero time in
    a loop), so after roughly eight minutes it passed 2,147,483,647 and
    went negative. The "log the first 200 calls" check then passed for
    every call, and each one printed and flushed a line to the log:
    millions of disk writes a second, the frame rate dropped to around 40
    with long hitches, other threads (input, audio) were starved, and the
    log grew by about 100,000 lines a second. The counter is now 64-bit
    and unsigned; tested by starting it just below the old limit and
    playing Amon Hen past it -- no log flood.
  - KeDelayExecutionThread looked up RECOMP_DELAY_STATS with getenv() on
    every call (millions a second); it is now read once.
kernel/xbox_memory_layout.c
  - The [NV2A] DMA_PUT line, and a log flush, ran on every pushbuffer kick
    in normal play: the trace switch is also armed by RECOMP_PB_EXEC, which
    every run sets. They now need RECOMP_NV2A_TRACE.
kernel/nv2a_pb_exec.c
  - The "[GPU] draw #" heartbeat is every 100,000 draws (was every 200:
    thousands of log lines a second).

-- 1.1.3 -------------------------------------------------------------------
usb/ohci.c, usb/usb_gamepad.c, usb/usb_gamepad.h
  - Rumble: the game's output reports on the pad's interrupt OUT endpoint
    (report 0, length 6, left and right motor speeds as 16-bit values) were
    accepted and dropped, so the controller never vibrated. They now go to
    xbox_InputSetState (XInput), which sends only changes.
  - RECOMP_USB_OUTLOG=1 (testing): the first 40 OUT transfers to the pad.
apu/apu_xaudio2.c
  - Lost output recovers. XAudio2 stops for good when its device goes away
    (headphones unplugged, default device switched, a display asleep, a
    driver reset): the queue stays full and every submit is dropped, while
    the game plays on and its music stream keeps being read -- silence, as
    reported after an upgrade screen. A critical error (engine callback), a
    failed submit, or a queue full and not draining for 750 ms now rebuilds
    the engine on the default device (at most every 2 s) and logs it.
kernel/nv2a_d3d11.inc
  - xbox_FrameTextureList: last frame's texture hashes (for finding what a
    screen is made of).
src/e3hud*.c/.inc/.h (game, ECTS HUD mod)
  - The mission results screen (front-end screen 10) drew the thermometer,
    so the mod took it for play and drew its in-play gauge and portrait in
    their in-play places, over "Replay" and the hero's name. There it now
    draws its gauge over the retail one's place instead; the retail gauge's
    top (F7A501DB), frame and medallion (6EB4352F) and colour ring
    (ADE193F9) are cleared as they are decoded (in play they were already
    hidden by switching their HUD elements off).
  - The loading-screen tip about the Skill Meter (4F380BD7, Fangorn) gets
    the demo gauge and orb like the experience tip; its arrow is kept and
    run back to the orb.
  - Helm's Deep wall (17TOP1): the progress bar's portrait (BB82F059) is
    replaced by e3hud\level10.bmp when installed (the demo's picture).
  - TT_E3HUD_ELDUMP=1 (testing): each screen's textures, and the gauge
    elements, in the log.

-- 1.1.4 -------------------------------------------------------------------
Fixes a regression in 1.1.3: on some PCs no input worked at all (controller,
keyboard or mouse).

usb/usb_gamepad.c, input/xinput_device.c, input/xinput_xbox.h
  - 1.1.3 passed the game's rumble report to XInput from the USB
    emulation's thread. The game sends one ("motors off") while it is still
    connecting the pad, and that first XInput call loaded XInput and
    scanned all four slots (slow when they are empty) right there: the
    connection timed out, the game never saw a pad, and the keyboard and
    mouse -- which reach the game through that same pad -- went with it.
    The rumble is now only recorded there (xbox_InputSetVibrationDeferred)
    and sent by the next input poll.
  - The controller scan, including the first one, runs only on its own
    thread; until it answers (a few ms) the pad reads as absent and the
    keyboard covers it.
  - XInput is loaded once under INIT_ONCE; it used to mark itself loaded
    before loading, so a second thread could see no XInput meanwhile.

-- 1.1.5 -------------------------------------------------------------------
usb/ohci.c
  - The emulated pad was plugged in the moment the game's USB driver
    enabled root-hub interrupts. On a fast PC that can be while the
    driver's own bring-up scan is still running, which clears the connect
    change unseen: the log said "operational after 0 ms", two RHSC
    interrupts, and then no transfer ever -- no pad, and so no keyboard or
    mouse either (they reach the game through the same pad). The plug now
    waits 200 ms after the driver is listening (the frame clock keeps
    running meanwhile), and if the driver has not sent the device a SETUP
    within a second, the pad is presented again as if re-plugged (up to
    ten times, logged).
  - RECOMP_USB_MISS_FIRST=1 (testing): the first arrival is lost, to
    exercise the re-plug.

-- 1.1.6 -------------------------------------------------------------------
usb/ohci.c
  - Rumble, for real. XInputSetState sends rumble to this pad as a HID
    SET_REPORT control request (it has no OUT pipe open), not on the
    interrupt OUT endpoint 1.1.3 listened on. And no control request made
    after enumeration was ever serviced: the lists were walked one at a
    time, each publishing its own done list, and the next skipped while
    that was unacknowledged -- the pad's input report completes on the
    periodic list every tick, so the control list was never reached again.
    The game's first rumble request stayed pending, and as it sends the
    next only when the last completes, it never sent another. Periodic,
    bulk and control lists are now walked in one pass into one done list,
    published once; SET_REPORT's data stage (class/interface request 0x09)
    is passed to the controller.
src/twotowers_manual.c (game)
  - XInputSetState (0x00169845) wrapped: TT_RUMBLE_LOG=1 (testing) logs its
    first calls, result and the device fields it checks.
-- 1.2 -------------------------------------------------------------------
kernel/kernel_hal.c, kernel/kernel.h
  - xbox_CpuLockYield: lets go of the dispatch lock (all of this thread's
    holds) for a moment and takes it back.
  - xbox_DpcLockGuestCs(cs, max_ms): for a guest lock helper that skips its
    critical section at DISPATCH_LEVEL -- takes it anyway, yielding the
    dispatch lock while it waits; after max_ms it gives up and the caller
    goes ahead unlocked, as on the console.
src/twotowers_manual.c (game)
  - Crash in Helm's Deep (17TOP1) at 60 fps / unlocked: an access violation
    reading 0xFFFFFFEC in XACT (0x00135821, a sound bank stopping its cues,
    called from the game's sound code). XACT's lock (0x00134940) takes its
    critical section only at PASSIVE_LEVEL: at DISPATCH_LEVEL it is a DPC,
    which on the console runs with every thread stopped. Here DPCs run on
    the timer thread beside the game thread, so the game walked a cue list
    the DPC was relinking and read a null link. More cues start and stop per
    second at a higher frame rate, so that is where it showed. The lock now
    takes the section at DISPATCH_LEVEL too (xbox_DpcLockGuestCs, 50 ms).
  - @IncVillagerRescue (0x000CC9C0) wrapped: logs the villager count.
src/e3hud*.c/.inc/.h (game, ECTS HUD mod)
  - Westfold (16ROHAN2): the villager bar (HUD list 11: a green bar and a
    portrait, 8 steps -- the count at 0x4A55BC starts at 8, one off per
    @IncVillagerRescue) is replaced by the demo's two rows of four villager
    couples, a red cross over each one lost, from e3hud\villager.bmp and
    villager_dead.bmp (crops of the ECTS footage) when installed; without
    them the retail bar stays. TT_E3HUD_VRTEST=n (testing) shows it with n
    left.
  - Helm's Deep wall: the level10.bmp picture is lowered 4 units (its HUD
    element, while drawn) so it is centred on the bar, not 4 units above.
  - TT_E3HUD_LISTDUMP=1 (testing): every element of the HUD lists that are
    on (and list 11) in the log.
input/xinput_device.c, usb/usb_gamepad.c, usb/ohci.c
  - Controls freezing in play with the last input held (Gimli aiming at the
    breach, Aragorn walking into a wall at Westfold; the level-select map
    then scrolled by itself). The USB emulation's thread built every pad
    report by calling XInputGetState, and since 1.1.6 also sent rumble with
    XInputSetState, right there; XInputSetState can block on the pad's
    driver (Bluetooth, Steam Input, virtual pads), and while it did no report
    went out, so the game kept the last one. XInput now runs only on a poll
    thread (every 2 ms, into a table the reports are read from) and a rumble
    thread; a pad whose poll stops answering reads as at rest after 250 ms
    instead of frozen. The log reports a slow XInputSetState and any stretch
    of more than 3 s with no pad report sent to the game.
src/e3hud.c, e3hud_tex.inc, e3hud_layout.h (game, ECTS HUD mod)
  - Westfold's villager couples are just the faces now, as in the demo
    (villager.bmp / villager_dead.bmp cut from the footage, background keyed
    out), in their own shape against the right edge of the retail art.
src/prompts.inc, src/e3hud.c (game), video/gfx_menu.c/.h, src/twotowers_host.h
  - PlayStation button prompts: [Input] prompts = auto / xbox / playstation
    (also "Button prompts" in the F10 menu). The game's button textures --
    A 13DF2C89, B 62764416, X 6BB46B89, Y 5E463DBF, right trigger 71E4228A,
    seen on the upgrade screen's combo lists and "Help" -- are redrawn as
    they are decoded: cross, circle, square, triangle (same places on the
    pad) and R2. "auto" uses them when player 1's pad is a PlayStation one.
apu/apu_xaudio2.c, kernel/nv2a_d3d11.inc, platform/d3dcompile_dyn.h,
platform/mf_dyn.h, d3d/*.c, audio/wma_decoder.c, video/video_player.c
  - Windows 7: the game imported XAudio2_8.dll, d3dcompiler_47.dll and
    Media Foundation, which a Windows 7 (and the N editions of any Windows)
    may not have -- and a missing import stops a program before it starts.
    All three are now loaded at run time: XAudio2 2.9 / 2.8 or else the
    waveOut output; the newest HLSL compiler present (47 down to 43, the
    DirectX redistributable's) or else the software renderer; Media
    Foundation where it is installed.
  - The Direct3D 11 device is asked for feature level 11_0 exactly (the
    shaders are shader model 5): a 10.x card, or a Windows 7 without the
    platform update, now gets the software renderer and a line in the log
    instead of a device whose every shader fails. The log names the card
    (vendor and device id).
apu/apu_xaudio2.c (Linux)
  - Sound on Linux, through SDL2's audio (48 kHz stereo, ~85 ms of queue,
    reopened if the device goes away), the same contract as XAudio2.
src/linux_setup.cpp, linux/TwoTowers.sh (Linux)
  - twotowers-setup: checks and extracts the disc image (or uses an
    extracted folder), edits a few settings, adds a menu entry.
    TwoTowers.sh runs it on first start (with zenity dialogs when zenity is
    installed) and starts the game; it says how to install SDL2 if missing.
  - Built against glibc 2.27 (Ubuntu 18.04 and later, and the distributions
    of that age); SDL2 comes from the system. No OpenSSL needed.
platform/xbox_winnt.h (Windows builds)
  - The real cause of the controls freezing (the input thread change above
    was not enough: it went on happening). XBOX_THREAD_LOCAL was
    __declspec(thread) for every Windows compiler, but with MinGW that is
    __attribute__((thread)), which does not exist: the compiler warned and
    made each such variable an ordinary global. So the IRQL, the dispatch
    lock's hold count, the critical-section cache and the per-thread wait
    timers were shared by every thread in the Windows builds (Linux was
    right). One thread lowering its IRQL released the dispatch lock another
    thread held, and DPCs ran in the middle of the game thread's raised-IRQL
    sections; the USB driver then gave one transfer descriptor to the rumble
    request and the pad's input endpoint at once, and the pad's reports
    stopped for good with the last input held. Now __thread under GCC/clang,
    __declspec(thread) only under MSVC. ("KfLowerIrql(2) while at 0" in the
    log was this too, and is gone.)
usb/ohci.c
  - An endpoint's head pointer is moved past a transfer descriptor with an
    atomic compare-and-swap before the transfer is done, and not at all if
    the driver changed it meanwhile; a descriptor with no valid NextTD is
    left alone. (Before, a stale head was written back over the driver's.)
  - The stall report (no pad report for 3 s) now lists the last 512 things
    that happened to the endpoints and controller registers, and the TDs.
kernel/kernel_hal.c
  - [IRQLBUG] lines come with the thread's last IRQL changes.
kernel/nv2a_sw3d.inc
  - RECOMP_TEX_DUMP_ALL=1: the texture dump includes the large textures
    (loading pictures) too.
input/xinput_device.c
  - xbox_InputPlayerPadPlayStationModel / xbox_InputAnyPlayStationModel:
    which PlayStation pad (3, 4 or 5) a player holds / was last connected.
src/prompts.inc, src/prompts_sig.h, prompts\*.bmp (game)
  - With PlayStation prompts, the loading screens' controls pictures (the
    first levels': UI\loadl2..4 -- "Parry", "Fierce attack" / "Fire", and
    the full layout) show a DualShock 3, DualShock 4 or DualSense, whichever
    player 1 holds (else the last PlayStation pad connected, else a
    DualShock 4; [Input] ps_pad = auto / ps3 / ps4 / ps5). The pads are
    drawn for this (no picture of a real pad is used), with their face
    buttons where the Xbox pad's were, so the game's own leader lines and
    labels still point at them; the left stick's line is re-routed, Start
    and the black button become OPTIONS/START and L1 insets, the triggers
    L2/R2. The "pressed" pictures flashed over the buttons follow. They are
    recognised by their look (a grid of mean colours), not by hash.
src/e3hud villager.bmp (game, ECTS HUD mod)
  - The left villager's mouth was cut off by the mask: the head shapes
    were widened and the mask's holes filled.
platform/win32_compat.c (Linux)
  - Crash entering Balin's Tomb (04BALIN1) on Linux: a loader thread waited
    on an event handle before its creator had stored it, and the Linux
    Win32 layer took the garbage value for an object and locked it
    (pthread_mutex_lock on a wild pointer). Live objects are now kept in a
    table; a handle that is not one is refused (ERROR_INVALID_HANDLE), as
    Windows does, in every call that takes one.
CMake / toolchain (Windows)
  - The Windows build links against msvcrt.dll (llvm-mingw's msvcrt
    toolchain) instead of the Universal C Runtime, so Windows 7 and 8.1 no
    longer need KB2999226 or a Visual C++ redistributable.
src/heroes.c, kernel/kernel_path.c (1.2, highly experimental, off by default)
  - Extra heroes mod ([Mods] extra_heroes=1, off by default; a choice in the
    launcher's settings window and in the Linux setup menu). Once a save has
    finished Hornburg Courtyard and the Tower of Orthanc (the game's own test
    for Isildur), the character select has eight places: Aragorn, Legolas,
    Gimli, Isildur, Boromir, Gandalf, Frodo and Lurtz, on one turning ring.
    The new four use the game's own models and animations (taken from Balin's
    Tomb and Amon Hen, copied into a private copy of UI\mainmenu.bff under
    heroes\), stand in the menu stance and carry their own name and level
    label. A guest plays as hero 4 with its own skin in the level (a private
    copy of the level file), and has its own experience, scores and upgrades,
    kept in heroes\records.dat; the save itself keeps Isildur's.
  - Re-rigging: a new hero's model has its bone tables ({skeleton bone,
    model bone} per model part) pointed at the matching bones of the
    heroes' skeleton (Aragorn's, shared by Isildur), so the hero's own
    animations drive it: every move, the sword and bow, the level's opening
    scene and the character select's poses. The bone tables were made by
    twotowers/tools/rig/chain.py (limbs matched along their chains, the rest
    following the nearest limb bone or the spine/head by height).
  - Unlock: a save that has finished Hornburg Courtyard and the Tower of
    Orthanc; extra_heroes=2 answers the game's two checks with "finished"
    while it runs (nothing is written to the save).
  - kernel_path.c: xbox_path_redirect, a hook that lets the game serve a
    file from another place on disk.
src/e3hud.c (ECTS HUD mod)
  - Westfold's villager couples showed on the results and upgrade screens
    after the level: those screens draw the experience tube too, which was
    taken for "in play". Play is now the health sword being drawn.
-- 1.3 -------------------------------------------------------------------
src/heroes.c (game, Extra heroes mod, highly experimental, off by default)
  - Unlocking one by one: each finish of the Tower of Orthanc (21ORTH2) as
    Isildur himself counts, per save (heroes\unlocks.txt, by save folder):
    2 unlock Boromir, 3 Gandalf, 4 Frodo, 5 Lurtz. A locked hero shows as a
    faint shadow on the select and is skipped by the cursor.
    extra_heroes=2 still shows all four on any save.
  - The guests play on their own skeletons with their own motions. Each
    level copy gets a composed animation set: the guest's skeleton (with
    the heroes' aiming blocks added to its "skel" chunk, so ranged attacks
    fire), and in each of the hero's slots one of the guest's motions
    carrying the hero's events and tail (strike, swoosh, combo windows,
    strike bone) re-timed to it. Walks and runs play at the hero's ground
    speed for the guest's stride; the guest's swing (its fastest hand
    moment) is warped to near the hero's strike event; attacks run no
    slower than about 1.15x the hero's.
  - Boromir's and Frodo's light, heavy, kick and ranged attacks are the
    hero's own, retargeted onto their skeletons at level build
    (retarget_rec: world rotations through the bind poses, root motion
    kept). The guest's weapon-hand bones (attach points 0 and 1) take the
    hero's hand rotations, not the bone the rig map gives them (Boromir's
    sword hand maps to the hero's wrist): before, the blade swept beside
    the hero's and heavy attacks found no one. Gandalf and Lurtz keep their
    own attacks. Walks, runs, blocks, hit reactions and deaths are the
    guests' own.
  - Strike reach: the melee hit test (sub_000BA110) samples the weapon from
    WEAPON_MIN_EXTENTS to WEAPON_MAX_EXTENTS along its local +Y through the
    weapon's world matrices (+0x60, last frame's +0xA0). The vector
    transform sub_000FF13D (stdcall: out, v, m) is wrapped: for the
    player's weapon (id 1) while a guest plays, the samples reach further
    along the blade (x1.35; Frodo x1.6 for Sting's short blade). Measured
    in Hornburg Courtyard, heavy-attack hits went from none to on a par
    with Aragorn's.
  - Weapons: the hero's records name two sword-hand models, 4586A62C and
    13F82514 (the one drawn in his hand as he fights); both, and the bow,
    become the guest's, in the level's weapon records and in the weapon
    script op's resource lookups. Boromir: his sword, and his shield built
    into his model on his left forearm (his texture made 256x512 to hold
    it; DXT1 codec). Gandalf: his staff in his sword hand at all times, an
    empty prop in the bow's. Frodo: Sting, with its second texture layer,
    its steel tinted a pale blue as it is copied in. Lurtz: his Uruk-hai
    sword and his own bow.
  - Gandalf's shots are a magic beam: a shot whose shooter (the bow-hand
    weapon object, model at +0x494; that model's key at +4) is his is
    always the magic bolt (projectile 15), and as it leaves a line of
    glowing particles runs from the staff's head (the staff object's
    world box) to the aim point. The projectile launch and impact
    (0xD9460 / 0xDA180) are wrapped for this.
  - Each guest's own idle pose on the character select (and his own pose
    when chosen and confirm move), his own figure on the pause upgrades
    screen and the end-of-level 3D model, his own HUD portrait, upgrade
    and results-screen pictures (film stills from the game's own
    UI\stills*.BFF; for Boromir a cameo from his face texture).
  - Model fixes: per-part palette remap, stale bone indices, torn
    stencil-shadow data blanked; Frodo on Gimli's level variant.
  - Fix: a crash after the intro (Boromir's 256x512 texture overran its
    buffer: mips not counted).
  - Fix: the heroes missing from the select after returning to the menu.
  - Weapons everywhere his model is drawn (1.3 update):
    - The character select: each guest's sword-hand weapon is added to
      the menu file (prop 1200 / texture 1250, keys 0x31-0x34; Sting
      tinted) and attached with the game's own call (sub_000CA540), as it
      does for Aragorn's sword. Before, his hands were empty.
    - Cutscenes: the hero's in-hand sword model (13F82514) becomes the
      guest's weapon in every record that names it, not only the player's,
      so the cutscene copies of him carry it too. Before, they held
      Aragorn's sword.
    - The pause screen's upgrade figure: drawn from his model as he plays
      (Boromir's with his shield), with his weapon hung from his hand
      (placed by the hand's attach point in the bind pose, turned with
      the lowered arm, blade down by his side, Gandalf's staff upright).
src/twotowers_manual.c (game)
  - Wrapped for the above: 0xD9460, 0xDA180, 0xFF13D (and, for testing
    logs only, 0xBA110 melee sweep results, 0x9E9D0 script callbacks,
    0x8E470 particle spawns, 0xB4220 LightGlowEffect).
kernel/nv2a_d3d11.inc, nv2a_d3d11_vp.inc, nv2a_pb_exec.c
  - xbox_gpu_cpu_lo/hi: the title can ask for the models whose vertex data
    lies in a range to be transformed on the CPU. Testing switches
    RECOMP_BIGTRI, RECOMP_HWVP_DENY.
src/e3hud.c, e3hud_tex.inc (ECTS HUD mod)
  - A guest's own portrait in the medallion. The HUD is drawn only in play.
Testing switches (heroes.c): TT_SWEEPLOG (melee sweep hits per weapon,
  kills), TT_SHOTLOG, TT_WDBDUMP (the weapon table), TT_WREACH,
  TT_NOWAXIS, TT_RT_NOHAND, TT_NOSWORD2, TT_NOTINT, TT_NOTEX2, TT_NOFEW
  (no weapon on the select), TT_NOFIGW (no weapon on the
  upgrade figure), TT_FIGWRAW, TT_FIGDUMP (the upgrade figures written to
  heroes\fig_<name>.ppm).

-- 1.5 -------------------------------------------------------------------
src/heroes.c (game, Extra heroes mod)
  - Boromir's and Frodo's heavy attacks, kicks and light attacks aim where
    they face. Their melee moves are Aragorn's, retargeted, and Aragorn's
    sword stance turns his body about 40 degrees at bone 1 (the root's
    child). The game allows for that turn on the hero but not on a guest,
    so their blows went that far to one side of the one they faced. Their
    retargeted melee moves are now turned back by the hero's stance (bone
    1's turn in his fighting stance, slot 34, at its start). Measured in
    Hornburg Courtyard with heavy attacks pressed throughout (the player's
    sword's hit tests, sub_000BA110, and their hits): Boromir from about 10%
    of hit tests landing to about 18%, Frodo from 1% to about 25%;
    Aragorn's own about 14%. Lurtz (his own moves) about 15%, unchanged.
  - The retarget's rig map put right where it skipped a joint the hero
    has: the guest's root and its child take the hero's own (the root
    keeps no rotation keys, as every hero's and guest's own moves have
    it), and a bone between two mapped ones takes the hero's bone between
    them (the pelvis the hero's pelvis, the collarbones his collarbones,
    not his lower spine and neck). World rotations were copied before as
    well; this moves the shoulders and hips to where the hero's are, so the
    hands and blade are about 30% closer to the hero's.
  - Boromir's skeleton gets what every other hero's has: the "skel"
    chunk's two aiming blocks (they could not go in before: his chunk has
    no last word to put them before; now they go at its end) and attach
    points 6 (the spine bones turned toward a target) and 7 (head, spine,
    hands, pelvis, knees, feet), his bones for the hero's.
src/twotowers_manual.c (game)
  - The melee sweep and HUD wrappers also call testing logs (TT_HEADLOG).
Testing switches (heroes.c): TT_HEADLOG (at each of the player's sword's
  hit tests: the blade tip's and the victim's bearings from its owner, and
  every frame its hand and tip in the owner's frame), TT_RTYAW=degrees
  (the retargeted moves turned by this), TT_RTNOTURN, TT_RT_OLDMAP,
  TT_NOAIM2, TT_WREACH may now be negative.

-- 1.5.2 -----------------------------------------------------------------
src/heroes.c (game, Extra heroes mod)
  - Boromir's and Frodo's bow shot: Aragorn's draw hand holds an arrow
    (4586A62C) while his sword (13F82514) stands in the ground; the arrow
    became the guest's sword, so the sword hung in his raised draw hand.
    The arrow stays an arrow (Gandalf, who shoots from his staff, keeps the
    staff in hand); the sword in the ground is the guest's.
  - The level's own Boromir, Frodo, Gandalf and Lurtz (allies, Lurtz the
    Amon Hen boss) are no longer touched when one of them is played: the
    player's model, texture and moves go into the level under keys of
    their own (PLAYER_KEY). Before, the level's own copy took the player's
    moves (Boromir in Balin's Tomb stood stuck).
  - Only references next to the hero's model are pointed at the guest (his
    records, the cutscenes' copies of him, the level's list of his
    resources); characters that share the hero's moves keep them.
  - The prologue is Isildur's: whoever hero 4 is on the character select,
    the prologue loads Isildur and his records, and the guest comes back
    with the next level or the menu.
  - Retarget rig map: a bone the map hangs on another branch of the hero
    than its parent's follows its parent's bone (Boromir's coat panels and
    belt, under his pelvis, took the hero's spine: they swung out with his
    light attacks).
  - Character select: the guests play their stance from the start (they
    stood frozen in their set's first motion until chosen), and the ring
    is wider (radius 3 + 2.6, its front where the four's was), so the
    eight stand further apart.
src/e3hud.c (ECTS HUD mod)
  - The prologue's portrait is Isildur's: the game counts Aragorn as
    playing there (hero 1), so it showed his.
Testing switches: TT_FE_RING (the select's ring widening), TT_FELOG,
  TT_SAMEKEYS, TT_SWAPALL, TT_REFLOG, TT_ARROWSWORD, TT_RT_NOBRANCH,
  TT_SLOTLOG.

```

-- 1.6 -------------------------------------------------------------------
src/heroes.c (game, Extra heroes mod)
  - The first light attack: slots 55 and 56 (the speed attack's swing with
    nobody in reach, played on the first press) were taken for a parry, so
    the guests played a motion of their own from the parry pool there:
    Frodo his fall, Boromir his hit flinch. They are now the speed
    attack's: Boromir and Frodo swing Aragorn's swing (retargeted), Lurtz
    and Gandalf one of their own swings.
  - Frodo's model is matte: his model (Balin's Tomb's) has a strong white
    shine (header +0x14..+0x28); he looked wet or metallic on the select
    and in play. Made as the other guests' models are.
  - The end-of-level and upgrade-screen figure: the guest on his own
    skeleton with his own moves (as on the select), his set composed onto
    the figure's (key 9, Isildur's). Re-rigged onto Isildur's skeleton,
    Frodo's head and coat came apart. Lurtz keeps the old way.
  - Gandalf: a sword in his sword hand (the game has no Glamdring: the
    ranger's long sword, 13F82514), his staff in his left hand at all
    times, made part of his model as Boromir's shield is (shield_mesh2:
    the same for a model of two parts). His beam starts at the staff's
    head, on his left.
xboxrecomp kernel/nv2a_d3d11.inc, nv2a_gl.inc
  - Character shadows: the main depth buffer (640x480) and the shadow
    pass's 256x256 zeta share one address; a texture read there took the
    surface last bound as a target, which was often the wrong one, and the
    shadows came out as broken wedges and stripes. The surface whose size
    matches the texture is read (then the one last written: draws with
    writes on, clears, copies and CPU uploads now count as writes).
    RECOMP_SURF_OLDWR=1 the old choice; RECOMP_SURF_LOG2 where they differ.
  - The level cache folder carries the version on its own
    (<name>_own_b<version>_<hash>): with the version only added into the
    hash, an older build's folder could have the same name, and its stale
    levels were loaded (Frodo missing from Caradhras' opening scene, and a
    crash there and in Amon Hen).
Testing switches: TT_KEEPSHINE, TT_FIGREMAP.

-- 1.7 -------------------------------------------------------------------
src/heroes.c (game, Extra heroes mod)
  - Three more heroes, seven in all: Elrond (the prologue's, with the
    elves' curved sword; Aragorn's attacks, kick and parry fitted to him),
    Sauron (the prologue's, his mace and his own swings; a bolt of power
    for a bow, as Gandalf's) and the black Uruk-hai (the Berserker of
    Fangorn and Helm's Deep, his great sword and his own attacks). Their
    rig maps made by nearest bind-pose bone, Elrond's refined against the
    hero's attacks. Unlocked by the 6th (Elrond), 7th (the Uruk-hai) and 8th
    (Sauron) finish of the Tower of Orthanc as Isildur (s_unlock_at); until
    then a faint shadow on the select that the cursor passes by.
  - Their skeletons get the attach points every hero has (4, 6, 7, 8);
    Sauron's feet (points 2, 3: his helm's spike) are his ankles, his root
    at the heroes' height, and his walks (slots 0, 1) at the heroes' pace:
    at his own slow pace Caradhras' opening walk left him on the rock above
    the path, and he fell off the cliff. His slots 9 and 10 (standing) are
    the heroes' moves.
  - The parry (slots 55, 56) is a parry again; Boromir, Frodo and Elrond
    play the hero's, retargeted; Sauron and the Uruk-hai a kick or swing.
  - Retargeted moves: the root's ups and downs scaled to the guest's
    height (the hero's crouch put Frodo on the ground in his first swing).
  - The character select: eleven characters (the menu's character
    manager asked for room for twelve: tt_fe_room, wrapping sub_000CA6D0);
    the menu file's guest sets keep only the select's three moves (the
    file grew too big to load); 48 resources per built file.
  - Upgrade pictures: Elrond's and Sauron's cameos from their textures,
    the Uruk-hai's a film still of the Uruk host; their names drawn.
  - records.dat holds seven guests (TTH2); 1.6's (TTH1) is read.
  - Attacks land better. Light and heavy own-swing moves (Gandalf, Lurtz,
    Sauron, Uruk-hai) line up fully with the hero's strike, and their feet
    follow the hero's footwork (bone 0's x and y from the hero's root).
    The blade test reaches further (s_wreach), and for the guests whose
    blade is carried high (Gandalf, Sauron) it runs lower (s_wdrop): their
    heavy-attack hit rate in Hornburg Courtyard rose from about 5% to 12%,
    with the others' (13 to 20%).
  - The Uruk-hai's ranged attack: a burning rock, thrown with his own throw
    (slot 110) wherever the archers would shoot. The thrown-rock projectile
    (10); his rocks fly on their own copy of its projectile-table entry
    (in a block of contiguous memory) with the fire arrow's impact (blast 7,
    fire sparks 53), so they burst into fire where they land; nobody
    else's rocks change. They leave 3.5 units ahead of his hand (from the
    hand the rock struck his own body), and trail embers (particle 16) and
    a flame (4) in flight. Testing: TT_URUKPROJ, TT_FIREROCK, TT_ROCKPART,
    TT_ROCKFLAME, TT_ROCKAHEAD, TT_ROCKFROMHAND, TT_ROCKLOG, TT_SHOTDUMP.
src/e3hud.c: twelve portraits (the seven guests'); e3hud\elrond.bmp,
  sauron.bmp, uruk.bmp in the release.
src/twotowers_manual.c: sub_000CA6D0 wrapped.
src/setup_launcher.cpp, src/linux_setup.cpp: the Extra heroes setting names
  all seven ("eleven heroes").
tools/names/make_names.py: Elrond, Sauron, Uruk-hai.
Testing switches: TT_POSLOG, TT_NOPTFIX, TT_NOPTADD, TT_NOROOTLOW,
  TT_RT_NOVSCALE, TT_MENUALL, TT_FE_ROOM8.

-- 1.8 work --------------------------------------------------------------
src/heroes.c (Extra heroes)
  - The character select shows all the extra heroes from the first time
    it opens (after the second mission), as faint shadows the cursor
    passes by, and Isildur's place as one too until he is unlocked
    (place_locked); each still unlocks by Isildur's finishes of the Tower
    of Orthanc.
  - The kick (slots 50, 105) is the hero's, retargeted, for every guest:
    the game's kick lands where the foot goes, and the own-swing guests'
    swings in its place knocked nothing down (the Deeping Wall's
    ladders). Not turned by the hero's stance as his swings are (turned,
    Boromir's kick went 39 degrees beside, and the ladders fell only to a
    kick from the side). TT_OWNKICK, TT_KICKTURN the old ways.
  - Sauron's mace in its own texture (B4F6DCDD; 7C9741B8 was an 8-bit
    map, drawn as a pale see-through mesh).
  - Sauron tougher: in the levels built for him the player's record in the
    character database (type 910) has 1600 health (1000), a stagger
    threshold of 600 (200) and a weight against knockbacks of 10000 (100,
    the cave troll's); his flinches play in half the hero's time.
    TT_NOTOUGH the old way.
  - The kick turned for Boromir and Sauron (s_kickyaw, 30 degrees to their
    right). Measured at the Deeping Wall's first ladder (siegeLadderObj19:
    its matrix, the name's pointer +72, lies flat once kicked down), the
    guest put square to it or 1.5 to either side, one kick: Aragorn fells
    it square on and with it to his right; Boromir only with it to his
    left, Sauron only square on. Turned, Boromir fells it square on and on
    both sides, Sauron as Aragorn does. Elrond, Gandalf, Lurtz and the
    Uruk-hai fell it as Aragorn does or wider (Elrond from 4.5 back, where
    Aragorn's kick misses). TT_KICKYAW tests a turn.
  - The parry (slots 55, 56) is the hero's, retargeted, for every guest
    (his own swing, kick or stance in its place before). TT_OWNPARRY the
    old way.
  - Deaths: slot 24 is no longer taken from the guest's own set (in the
    enemies' sets it is not always a death: Elrond half fell, and Boromir
    and Gandalf played 30, getting up, and stood). Gandalf, Frodo and
    Elrond fall with their own knockdown (27, which ends on the ground),
    the Uruk-hai with his own death (24); Boromir (his ends on his knees),
    Lurtz and Sauron (their bosses' deaths leave them standing) with the
    hero's, retargeted. Found by the motions' height at their end
    (TT_SLOTSCAN).
  - The Uruk-hai's blade sampled 2 times its length for hits (1.65): his
    heavy blows found 9% of the hit tests in Hornburg Courtyard, 13% at
    1.9, Aragorn's 15 to 18%.
  - The pause screen's upgrade figure is posed: his own model skinned to
    his own stance (slot 34, or 0), the weapon in his hand, instead of the
    bind pose with the arms forced down.
  - Upgrade pictures from upgpics\<name>.bmp beside the program (24 or 32
    bit, up to 2048 square), fitted over the picture's left 280x256
    (elrond.bmp, sauron.bmp and boromir.bmp go with the build).
  - The upgrades are named for the hero (startup.BFF's strings, rewritten
    in place, never longer than the originals): "Sauron's Swift Terror",
    "Strength of Barad-dur", "Dark Bolt", ... TT_NOUPGNAMES the old way,
    TT_UPNLOG logs them.
  - Testing switches: TT_PPOS, TT_PTP (the player put somewhere,
    "x,y,z,heading,frame[,ms]"), TT_LADFIND, TT_LADMAT, TT_HITLOG,
    TT_SLOTSCAN, TT_TESTHP, TT_KICKYAW, TT_CBRARE, TT_FIGBIND, TT_FIGSLOT,
    TT_FIGT.
  - HEROES_BUILD 72.

-- 1.9 work --------------------------------------------------------------
src/heroes.c (Extra heroes)
  - The character select's order: Sauron last, after the Uruk-hai
    (s_place_order; the places' guests through place_actor/place_guest).
  - Sauron's bolt and beam red: effect 0x135's emitter colours (ABGR, two
    per emitter) turned to red as long as Sauron plays (red_effect, every
    64 frames). TT_NOREDBOLT the old way, TT_BOLTRGB tests a colour.
  - The Uruk-hai's heavy attacks: 43 then 69, his two swings that strike
    forward (113, a guard, looked like a parry and struck nothing; 41 a
    charge). Measured in Hornburg Courtyard: 234 heavy swings, 28 hits in
    240 s (135 and 13-17 in 300 s before).
  - Frodo throws a stone (projectile 10, the plain rock) instead of Gimli's
    axe. TT_FRODOAXE the old way.
  - The HUD's ammo counter (the level's 64x128 textures, keys 6 and 0xD) is
    the guest's own: Elrond's Legolas's (copied from the Legolas variant of
    the level), the others' a picture from hudammo\<name>.bmp (64x128,
    32-bit with alpha) under the level's disc. Gandalf: his staff; Frodo: a
    stone; the Uruk-hai: a burning stone; Sauron: his gauntlet, glowing red.
    Boromir and Lurtz keep the bow. TT_NOHUDAMMO the old way.
  - The upgrade screen's item pictures (upgradei/upgbigi.BFF, type 350,
    128x256 DXT5): the bow's six (keys 0x11-0x16) from
    upgitems\<name>_ranged.bmp (Lurtz's from a render of his own bow), the
    sword's four (0x17-0x1A) a render of the guest's own weapon (Sauron's
    mace head up), each with the upgrade colour's glow. TT_NOUPGITEMS the old
    way.
  - Voices: in play, the guest's own for Aragorn's (ids 1, 3, 4, 6 effort;
    7, 8, 10 struck; 11 death; 12 last breath): a copy of the level's bank
    (sound\WB_<level>.xwb) in heroes\voices whose entries for those ids
    point at his (written after the bank's own data: the game reads the
    whole file into its 3 MB of sound memory; entries past the bank's own
    count are never played, so the count stays). Taken from the game's own
    banks: Elrond Legolas's; Frodo Legolas's, higher; Lurtz his own (Amon
    Hen); the Uruk-hai theirs; Sauron Lurtz's, far lower; Gandalf and
    Boromir Aragorn's, lower. Pitched by the rate in the entry's format,
    shortened with a fade where long (Xbox ADPCM decoded and encoded again).
    On the character select, as a guest comes to the front (and as the
    select opens on him), his own line in place of the one Aragorn and
    Isildur share (2728): Gandalf's and Frodo's from the streamed banks
    (S_WB_04BALIN1: GA_VO4, FRxVO36A), Elrond Legolas's, the others one of
    theirs. The select bank (heroes\voices\select.xwb) carries them; the
    entry points at the guest's for the call only. TT_NOVOICE the old way.
  - Testing switches: TT_SNDLOG and TT_SNDMEM (the sound ids played, given
    a hook on the id lookup, 0x000EAA30, which release builds do not have), TT_SELVTEST, TT_PARTDUMP, TT_TAILDUMP, TT_SHOTDUMP,
    TT_SHOTLOG, TT_UPGITEMDUMP, TT_ANIMWATCH.
  - HEROES_BUILD 74.

-- 1.9.1 work ------------------------------------------------------------
src/heroes.c (Extra heroes)
  - A crash loading Fangorn and the Helm's Deep levels as Sauron, Lurtz or
    the Uruk-hai: their voices made the level's sound bank bigger than the
    game's own (Fangorn's 2.48 MB grew to 2.61 MB), and the sound memory it
    shares with the level's other sounds ran over (a heap the game walks
    next was overwritten). The guest's bank is now never bigger than the
    level's: the data is laid out again without the replaced voices', and
    his take that room, all shortened alike (with a fade) when they need
    more.
  - Gandalf has Aragorn's voices again (in play and on the select), Frodo
    Legolas's as they are.
  - The character select's confirm (A): the game's sound for it (2742 and
    2743, a sword's stab, the same for every hero) is the guest's own while
    he is in front: his weapon's swing with his cry over it (Boromir a sword
    and Aragorn's cry, deeper; Frodo a short blade and Legolas's cry; Elrond a
    sword and Legolas's cry; Lurtz his sword and cry; Sauron a heavy swing
    and Lurtz's cry, deeper; the Uruk-hai his blade and roar; Gandalf the
    game's). The select line (2728) the same way, also when the game plays
    it (the select opening on him). Both are put back once the select is
    gone (0x00076EF0's frames, or the HUD's, 400 ms without the select).
    Boromir's select line is Aragorn's, deeper; Frodo's and Elrond's
    Legolas's.
  - Elrond's confirm move on the select is his sword swing (66): his set has
    no other swing, and 33 is a stance.
  - Isildur's finishes of the Tower of Orthanc are counted when the save
    after it has been written, on the save it wrote (or else the one
    loaded): the save screen's list opens every save's game.xsv, and the
    count went to the last save in the list.
  - HEROES_BUILD 75.
e3hud\gandalf.bmp (new, with the build)
  - Gandalf's portrait for the ECTS HUD's medallion (rendered from his model
    in the medallion's ring); he had none, and the medallion was empty.

-- 1.9.2 work ------------------------------------------------------------
src/heroes.c (Extra heroes)
  - Frodo throws stones in every level: no bow (an empty bow hand, as the
    Uruk-hai's; his draw hand empty too), and every shot from that hand the
    stone (projectile 10). 1.9's stone replaced only the Gimli variant's
    axe (15), and he plays Aragorn's (the bow and its arrows).
  - The guests' own moves take the hero's footwork (bone 0's position keys
    along the ground) for his swings -- as meant since 1.7, but the test of
    where his bones' table lies had it before his events (it is after),
    so it never happened -- and now also for aiming, parrying and standing
    moves, and for a move of his in a walking slot where the hero stays in
    place; where the hero's has no position keys, his root stays at its
    start. Lurtz's aim (his 187) held his root 319 units forward and his
    stance between shots (slot 160, his idle) walked 1154 every 0.8 s: he
    went forward as L2 was pressed (Sauron's aim is one of his swings,
    Gandalf's his own move). Measured in Hornburg Courtyard, L2 only: Lurtz
    moved about a fifth as far while aiming, and the rest is the fight around him.
    The Uruk-hai's hits with the hero's footwork: 57 of 312 swings (18%).
    TT_OWNROOT the old way; TT_ROOTLOG, TT_ROOTKEYS (testing) log it.
  - The character select's sounds for a guest only when he is chosen, as
    for the heroes: the game plays hero 4's "highlight" (2728) as he is
    chosen and his "select" line (2729) as the choice is confirmed (both
    Aragorn's, shared with Isildur) -- now his weapon, then his line
    (Boromir Aragorn's deeper, Frodo and Elrond Legolas's 2733, Lurtz,
    Sauron and the Uruk-hai theirs; Gandalf Aragorn's). Nothing as he
    comes to the front. 1.9.1 swapped 2742/2743 (the select's own, every
    hero's) and left 2729, Aragorn's line, for everyone.
  - HEROES_BUILD 76.

-- release 1.6 (renamed) -------------------------------------------------
The 1.7 and 1.8 to 1.9.2 work above was released as 1.6, replacing the 1.6
and 1.7 packages built before; README's notes relabelled.
src/heroes.c: the guests stand in their stance (34, 33) in the hero's
  standing slots (160 and the others with no move of their own): their 0,
  taken before, is a walk, and with the hero's footwork (1.9.2) they walked
  in place. HEROES_BUILD 77.

-- 1.7 work --------------------------------------------------------------
src/heroes.c (Extra heroes)
  - The character select shows every unlocked character's level, in the
    distance as for the game's own three: the guests and Isildur (the game
    labels characters 0..3 only; hook 24 on 0x00075430 draws the others'
    with the game's two elements, at the place 0x000CA670 + 0x000CAA50 gives,
    through the view and projection, faded with them). Character 3's own
    label shows its own level when a guest takes that number (hero 4's
    level, which is whoever's records it holds, set for that one call).
    A guest's level is read from his records (hero block +0x10, 0x000D2610).
  - Gandalf a little short of Sauron in toughness (health 1450, stagger
    threshold 480, weight 5000; his flinches 60% of the hero's time), Lurtz
    a little short of Gandalf (1300, 380, 2500; 72%).
  - The Uruk-hai's rising attack (the hero's slot 48, from lying to his
    feet, striking): his own getting up (his 30), not a swing from standing.
  - Isildur's finishes of the Tower of Orthanc are counted on the save the
    game writes -- the game.xsv opened for writing (the kernel's
    NtCreateFile now tells the path hook, xbox_file_open_write) -- and on no
    other; a finish not saved before the next level is not counted.
  - The guests' shots have their own sounds: the hero's bow release and
    arrow flight (sound ids 192 and 193 on the shooting moves' events)
    become ids 0x7E01/0x7E02, two entries added to the level's sound bank
    and table (heroes\voices\<guest>_<level>.tbl) with sounds made at
    load: Gandalf's bolt a bright rising shimmer, Sauron's a dark falling
    roar, Frodo's stone and the Uruk-hai's burning rock a whoosh (the rock's
    with fire). TT_NOSHOTSND keeps the bow's.
  - Gandalf's and Sauron's bolts fly unseen (only the beam shows).
  - HEROES_BUILD 78.
src/prompts.inc, src/e3hud_tex.inc
  - Keyboard / mouse button prompts: each prompt drawn as the key or mouse
    button bound to its action now (a keycap with the key's name in a
    stroke font, or a mouse with the button lit), redrawn when a binding
    changes. "Automatic" follows the last input: keys or a pad.
  - The prologue's hints in play (X, Y, A: the level's own 64x64 pictures)
    change with the prompts too (PlayStation, keyboard / mouse).
  - ECTS HUD: Frodo's portrait from the demo (e3hud\frodo.bmp); the cave
    troll's health bar the demo's (e3hud\boss_troll_bar.bmp and
    boss_troll_face.bmp, laid out as the retail bar so its fill drains the
    same way).
src/setup_launcher.cpp, src/linux_setup.cpp, src/twotowers_host.h
  - Button prompts (with Keyboard / mouse), Renderer (Direct3D 11 / Vulkan
    through DXVK / software) and HUD in widescreen in the launcher.
xboxrecomp: input/xinput_device.c
  - xbox_InputLastWasKeyboard(): which player 1 used last.
xboxrecomp: video/gfx_menu.c, kernel/nv2a_pb_exec.c, nv2a_d3d11.inc, nv2a_gl.inc
  - "HUD in widescreen": the HUD's pieces against the screen edge they are
    drawn nearest to (left third, right third), or centred as before.
  - "Button prompts": Keyboard / mouse.
xboxrecomp: platform/d3d11_dyn.h (new), kernel/CMakeLists.txt, d3d/CMakeLists.txt
  - d3d11.dll and dxgi.dll are loaded at run time, no longer imported:
    renderer=vulkan loads DXVK's from a dxvk folder beside the program (the
    same Direct3D 11 renderer on Vulkan), else Windows' own.
xboxrecomp: apu/apu_core.c, apu/apu_xaudio2.c
  - A cushion of sound at the output: below 5 periods queued (~27 ms,
    RECOMP_AUDIO_CUSHION) the next period is made at once instead of on the
    clock, so a moment's delay in a busy scene no longer empties the
    device's queue (gaps, crackling). The log says when it ran dry.
xboxrecomp: platform/win32_compat.c, win32_compat_host.c, kernel/kernel_file.c,
  video/host_text.c, video/fb_present.c; twotowers/CMakeLists.txt, macos/
  - macOS (x86-64; Apple silicon through Rosetta 2): exception handling
    from Darwin's thread state, no thread affinity, the program's path from
    _NSGetExecutablePath, no posix_fallocate, a forward-compatible OpenGL
    core context, the 64 MB main stack, rpaths for SDL2; build-macos.sh and
    TwoTowers.command. Not tested on a Mac yet.

-- 1.8 work --------------------------------------------------------------
src/prompts_kb.inc (new), src/prompts.inc
  - Keyboard / mouse prompts on the loading screens: the controls pictures
    (UI\loadl2..4, the Xbox pad) are drawn as a keyboard and a mouse, the
    keys and buttons in use lit, and at each of the game's arrows the key
    (and mouse button) bound to that pad button now -- drawn again when one
    is rebound. The "pressed" flashes over the pad are left out. The hints
    under them that name the pad ("Pull and hold the left trigger ... Press
    the A button ... the left thumbstick") are written with the bound keys
    (in place in the game's string table, padded to their length), and put
    back for the pad.
src/heroes.c (HEROES_BUILD 80)
  - Unlocks: Isildur's count belongs to the game in play, as the game's own
    progress does. Loading a save brings its count; every save written
    carries the count of the game in play (one more when it is the save
    after his Orthanc finish), whatever slot it goes to and whoever was
    played since; before anything is loaded, the newest save's. The save
    screen's list (it reads every save) no longer changes it. Boromir at
    the 2nd finish, then one more hero each finish, as before.
  - Gandalf's shots: the bow's sounds again (TT_GANDALF_SHOTSND=1: his
    1.7 sound); Sauron, Frodo and the Uruk-hai keep theirs.
xboxrecomp: platform/d3dcompile_dyn.h
  - Compiled shaders are kept on disk (shadercache\ beside the game): the
    game's vertex programs and combiners, compiled as they are first met,
    were a stutter each (a few to tens of milliseconds); met again -- in the
    same run or the next -- they are read back. RECOMP_SHADER_CACHE=0: off.
xboxrecomp: video/fb_present.c
  - The game window is per-monitor DPI aware: on a scaled display (a 4K
    screen at 175 %) it is drawn in real pixels, not small and stretched by
    Windows -- sharper, and Vulkan (DXVK) showed a black window that way.
    RECOMP_DPI_UNAWARE=1: as before.
xboxrecomp: platform/win32_compat.c, kernel/kernel_file.c, kernel/kernel_rtl.c,
  input/xinput_device.c; twotowers/CMakeLists.txt, tools/macho_stack.py
  - macOS build fixes (found cross-compiling): the exception handler's
    common part was inside the Linux branch; no sched_getaffinity; Darwin's
    st_mtimespec; a 32-bit LONG counter; SDL_GameControllerSetPlayerIndex
    returns nothing. The rpaths are passed to the linker outright; lld has
    no -stack_size, so macho_stack.py sets LC_MAIN's stack size after it.

-- release 1.6 (the 1.6, 1.7 and 1.8 work, published as 1.6) --------------
src/heroes.c (HEROES_BUILD 81)
  - Upgrade names per hero: the six "Isildur's ..." moves (Swift Terror,
    Deliverance, Death Charge, Gambit, War Rush, Judgment) have names of
    each hero's own (Sauron's Dance, You Shall Not Pass, Sting's Bite, ...);
    the ranged weapon's names and descriptions say what it is (stones,
    burning rocks, fire) instead of bows and Rohirrim arrows, and its first
    description "Hurl" / "Cast" for "Fire". Whole strings, each no longer
    than the original; the first description only in Aragorn's block.
e3hud\frodo.bmp, e3hud\gandalf.bmp (game files)
  - Frodo's portrait raised (his chin above the bar, his hair to the top);
    Gandalf's head smaller and lower, his hat's tip inside the medallion.
xboxrecomp: kernel/nv2a_d3d11.inc
  - Vulkan (DXVK): presented on the display's refresh even with vsync off
    (immediate presentation left the window black on a GeForce + Intel
    laptop -- not confirmed fixed); RECOMP_VULKAN_IMMEDIATE=1: as before.

-- release 1.7 (final) ------------------------------------------------------
src/heroes.c (HEROES_BUILD 86)
  - Cutscenes: Fangorn's cutscenes give the hero's copies their own set
    (8417651D, the hero's skeleton and the scenes' moves) beside his model.
    With the guest's model swapped in and that set kept, his model was
    posed on the hero's skeleton and came out torn into stretched slabs
    (and its stencil shadow smeared wedges over the screen). Beside the
    guest's model, a set with the hero's bone count is now the guest's own
    (skin_apply; TT_NOCINESET: as before).
  - Cutscenes' own clips (type 750 "brxb": records {u16 size, u16 type};
    0x0301 {actor, offset} gives an actor a bank "aniz" of clips, motions
    0x8000 + n): a bank of the hero's clips (51 bones) is made again for the
    guest -- every clip retargeted onto his skeleton (retarget_rec, a key
    every 33 ms, up to 255), the bank's "hier" count his -- appended to the
    cutscene, the record pointed at it, the cutscene put at the file's end
    and its directory entry repointed (cine_clips_build; TT_NOCINE).
  - The hero's motions the scenes play by number (his idle-class slots
    other than his stances) are the hero's, retargeted (TT_OWNSPECIAL).
  - The finishing thrust (57, 59; the upgraded 191, 193) is the hero's,
    retargeted, for every guest: Gandalf's, Lurtz's, Sauron's and the
    Uruk-hai's own swings stood in for it and the blade never turned down
    (TT_OWNSTAB).
  - Gandalf's health 1575, Lurtz's 1500 (the bar's length unchanged: it is
    drawn as a share of the most).
  - Gandalf's voice: Gimli's (17TOP1 265-273, the select line INTRFACE
    2731), at 0.93 of its pitch.
  - Testing: TT_STABSCAN (each hero motion's sword-hand axes).

-- release 1.7.1 -----------------------------------------------------------
src/heroes.c (HEROES_BUILD 87)
  - The last ranged upgrade: upgrade 0xF of the guest's block (0x4AC600 +
    3 * 0x50; the game's arrow-glow colours, sub_0009B4D0, test 0xF, 0xE,
    0xD, the three ranged upgrades). TT_RANGEDTOP=0/1 (testing).
  - Beam colours: effect 0x135's emitter colours kept as found and set as
    asked (effect_colour: own, red, white), so they can change back.
    Gandalf's white with the upgrade; Sauron's red, with the upgrade red
    and blue in turn (set as each beam is drawn; drawn particles keep
    theirs). TT_NOWHITEBOLT, TT_WHITERGB (testing).
  - The Uruk-hai's rock: plain (projectile 10 as it is) until the upgrade,
    then the burning rock; every third (TT_POTEVERY) is the fire pot
    (projectile 4) from its launch: its entry the rock's flight (+0x08 to
    +0x20) and the pot's landing (+0x24 on, no trail), its number 4 so it
    bursts where it lands or strikes (sub_000DA110's switch: 4 ->
    sub_000D7530, the burst and the burning ground). Landing on the ground
    often ends it unburst: then sub_000D7530 is called for it where it lies
    (a burst of one is seen by its particle 0x73 spawned from 0xD76AD near
    it). TT_NOROCKPOT, TT_NOPOTBURST (testing).
  - His first two ranged upgrades renamed Crag Rocks and War Rocks (the
    stones are plain until the last, Lava Rocks).
  - Testing: TT_PARTLOG names each particle's caller; TT_SHOTLOG logs how
    his rocks end.

-- release 1.7.2 -----------------------------------------------------------
xboxrecomp: input/input_bind.c, input_bind.h, xinput_device.c
  - A Sprint action (kb_sprint, pad_sprint): no controller input of its
    own; the title asks xbox_SprintHeld() (its keys, or its button on
    player 1's pad, the last read). Defaults Shift and the left stick's
    click, which the left stick click action no longer has (the game does
    not use it). RECOMP_SPRINT_TEST (testing: always held).
xboxrecomp: kernel/nv2a_d3d11.inc, nv2a_gl.inc
  - Menus fill the screen: a frame with no 3D (menus, upgrade and results
    screens, loading screens; not movies) is shown 640/593 larger about its
    middle -- the game draws them inside the TV safe area, scaled 593/640.
    RECOMP_MENU_FILL=0: as before.
src/setup_launcher.cpp
  - The Controls window: Sprint (Sauron); the left stick click unbound.
src/heroes.c (HEROES_BUILD 91)
  - Sauron's sprint: the hero's runs (2 and 5) retargeted onto him in two
    slots the hero's set leaves empty (205, 206); while Sprint is held his
    set's entries for 2 and 5 point at them (sprint_frame: his set found by
    its slot 205), and a run he is in is switched in his objects. The game
    moves him by his motion's root keys (his run 4.5 units in 0.96 s, the
    hero's 7.5 in 0.67 s). TT_NOSPRINT, TT_SPRINTLOG.
  - Lurtz and the Uruk-hai: the hero's speed and fierce attacks and kick,
    retargeted (rtscale 1, "sfk"; the Uruk-hai's rising attack 48 his own).
    Blade reach: Lurtz 1.35 -> 1.65, the Uruk-hai 2.0 -> 2.25. Lurtz: health
    1800, ENEMY_MASS 7000, flinches 52% of the hero's time.
  - Move slots: one whose root goes another way than the hero's (or barely
    moves) is the hero's, retargeted. The slots a guest's set lacked were
    all his 9 (Lurtz's and the Uruk-hai's: a step to his left), Sauron's
    4, 5, 9, 10 went forward or back where the hero's go the other way or
    sideways: in water and elsewhere he went off course. TT_OWNMOVES.
  - The pause screen's figure (the picture in upgradei.BFF, keys
    0x36/0x37): the bind pose with the arms lowered for everyone; the first
    moment of one of his own motions stood Lurtz mid-stride (his legs into
    one) and the others twisted. TT_FIGSTANCE, TT_FIGSLOT, TT_FIGSCAN.
  - The upgbigi.BFF figure: his stance in every slot; Lurtz on his own
    skeleton (TT_FIGREMAP: re-rigged, as before).
  - Character records: the fields named (the game's hash, sub_0006BE30:
    h += (h << (c & 7)) + c): 056A06A4 ENEMY_MAX_HP, F3023D1D
    ENEMY_EXPERIENCE, 2DD1F42B ENEMY_MASS.
  - Testing: TT_POSLOGF, TT_INPALL, TT_AWRESCAN, TT_ALLSTANCE.

-- release 1.7.3 -----------------------------------------------------------
src/heroes.c (HEROES_BUILD 92)
  - Lurtz and the Uruk-hai: their own attacks again, as asked (1.7.2's
    retargeted hero attacks undone: rtscale 0, no rtcls). Their blades'
    reach (1.65, 2.25), Lurtz's toughness and the move-slot fix stay.
xboxrecomp: input/xinput_device.c
  - Testing: RECOMP_PAD_SCRIPT_RAW (the pad script as the controller's own
    buttons, through the bindings), RECOMP_PAD_RAWLOG.
src/heroes.c (HEROES_BUILD 93)
  - The finishing thrust (57, 59, 191, 193) no longer the hero's: those slots
    are in the light attacks' chain, and Gandalf's and Sauron's light
    attacks had become Aragorn's (TT_HEROSTAB: the old way, testing).
  - The scenes' motions (1.7) the hero's only for 39 and 136; 35, 37, 38,
    156, 161 and 167 their own again -- 37 and 38 play as a fight starts
    (TT_ALLSPECIAL: the old way, testing).
1.7.4: src/heroes.c (HEROES_BUILD 94), src/setup_launcher.cpp, xboxrecomp input/*
  - Sauron's sprint removed, as asked (the Sprint action, its keys and the
    launcher's and F10's rows; Shift and the left stick click the left
    stick click's again, as in 1.7.1).
  - Sauron keeps his own move slots: the hero's, retargeted, put the hero's
    root heights on his lowered root, and he fell through the ground at the
    Gates of Moria.
1.7.5: src/heroes.c (fall_catch_frame; HEROES_BUILD unchanged, 94)
  - Sauron still fell through the floor at the Gates of Moria in 1.7.4 (at
    165 fps, unlocked). Not reproduced here (Wine, 5-16 fps; walks, fights
    and every upgrade; a slowed guest clock as a fast PC's). A new hero who
    drops steadily 20 below where he stood is put back on the highest place
    he stood in the seconds before (TT_NOCATCH: off; TT_SINK: testing, a
    made fall-through). TT_ZSCAN: testing, each composed move's root height.
1.7.6: xboxrecomp input/input_bind.[ch], input/xinput_device.c, video/gfx_menu.c; src/setup_launcher.cpp, src/twotowers_host.h
  - Walk (BIND_WALK, kb_walk; default B in both layouts): held with the
    keyboard's move keys, the left stick is pushed 45 % (RECOMP_WALK_PART,
    testing) -- the hero walks (about a quarter of the run's speed, measured
    at the Gates of Moria). Keyboard only; on a pad, the stick itself. In the
    launcher's Controls and F10 ("stick part way" in the pad column).
  - The controller's Start (reported not to work with a PS3 and an Xbox One
    pad, Start on the keyboard fine): its first presses logged with whether
    they reached the game, the window being in front and Start's binding.
    Through a controller here (RECOMP_PAD_SCRIPT_RAW, testing: the pad
    script pressed on a controller, a pad at rest made if none) Start pauses
    the game and confirms in the menus. Pad script: LSU45 / LSL45 / LSR45,
    the stick pushed part way.
1.7.7: xboxrecomp kernel/nv2a_d3d11.inc, kernel/nv2a_gl.inc, video/gfx_menu.c; src/twotowers_manual.c
  - Menu fill for the front-end screens that draw 3D too (the main menu,
    the mission select, the load screen's burning edges, the character
    select): the title tells the renderers (xbox_frontend_hint) when a
    front-end screen is up -- the front-end screen (0x4AD050) anything but
    play's 6 and 8 -- and such frames are zoomed as the 2D-only ones are.
    Play and the pause menu (screen 6) are left as they are. Logged as
    [FILL] on each change of screen.
1.7.8: src/prompts.inc, src/prompts_kb.inc, src/heroes.c, src/setup_launcher.cpp, src/linux_setup.cpp,
       src/twotowers_host.h, tools/prompts/{padart,compose}.py; xboxrecomp video/gfx_menu.c,
       kernel/nv2a_d3d11.inc, kernel/nv2a_gl.inc
  - Controller prompts from the bindings: each prompt texture (A B X Y RT,
    the prologue's X Y A) drawn as the pad source bound to that game button
    (pr_pad_src: A B X Y LB RB LT RT Start Back d-pad LS RS) in the style
    shown; the original Xbox style keeps the game's own picture while a
    button is bound as by default. Redrawn when a binding changes
    (xbox_bind_rev). TT_PADSRC (testing): "act:src,..." as if bound so.
    TT_PRDUMP (testing): every source in every style to prdump.bmp.
  - Xbox One prompt style (prompts=xboxone; F10, launcher, Linux setup):
    black buttons with coloured letters, LB RB LT RT, View and Menu icons.
    Loading screens: prompts\x1_loadl2..4, x1_lit_*, x1_l2_pressed.bmp
    (tools/prompts: padart model 1, an Xbox One pad drawn from geometry --
    its face diamond and left stick where the old pad's are, so the game's
    leader lines fit; the triggers, the Black button and Start as LT / RT,
    LB and Menu insets).
  - The loading screens' hints with a controller: the bound buttons'
    names in each style ("Pull and hold L2 ... Press the cross button").
  - Movies in widescreen (movie_fill): a wide picture chosen, a movie is
    drawn at the window's width at 4:3, its top and bottom cut -- the films
    are letterboxed at about 1.85:1 in their 4:3 frames. RECOMP_MOVIE_FILL=0.
  - Fall catch (Extra heroes): a drop of more than 15 in 100 ms was taken
    for a scene moving him and the places he stood forgotten -- a real fall
    at full speed was never caught; and a fall before he had stood anywhere
    had nowhere to go back to. Now only a jump sideways or up is a scene's,
    the place he came into play is kept, a place counts once stood 0.3 s;
    logged: "in play at", "dropping at".
  - [Developer] level10=1 (TT_LEVEL10, testing): as a save loads every hero
    at level 10 (the save's four in memory, the extra heroes' records.dat),
    earned experience at least 120000, the difference added to spend.
1.8: the 1.7.8 testing switch [Developer] level10 removed (it was for testing only).
1.8: movies as before by default (the fill looked too close); RECOMP_MOVIE_FILL=1 turns it on.
1.8: F10 / launcher options Menus (GFX_MENUFILL, [Display] menu_fill, RECOMP_MENU_FILL; fill by default) and Movies in widescreen (GFX_MOVIEFILL, movie_fill, RECOMP_MOVIE_FILL; as made by default), read live by both renderers.
1.9: the main menu's Exit and the mouse pointer in the menus (twotowers/src/menus.c).
  - Exit: an element of ours (a copy of Credits' at y 380, the text "Exit",
    at 0x4BA400 / 0x4BA500) drawn after the menu's own items (0x00078F50);
    down from Credits (0x00079180) moves the choice (0x4AD050+0x3864 +0x14)
    to 4 with the swoosh (0x000CB590) and the game's two move sounds; the
    colours (0x00079870) dim Credits and light Exit; A or Start on it ends
    the game (xbox_FramebufferRequestClose, fb_present.c: the window's close).
  - The pointer: every element drawn (0x000C8940), the game's lists drawn
    (0x000D0700: dialogs and the pause screen; +0xF0 chosen, +0xF4 first
    shown, +0xF8 count, +0x100 shown, +0xC0 enabled, +0x1C4 font) and a
    frame tick at the game's frame wait (0x000E00E0). A list's chosen item is
    the one in a colour of its own; pointing presses the d-pad (injected by
    xbox_pad_filter, xinput_device.c, the packet number moved) until the
    choice is there; a click is A, a right click B; a setting's values and
    sliders get left / right until the marker or the knob is at the click.
    The mission select (front-end screen 1, its object at 0x4AD058, +0x10 the
    place) walks the game's own neighbour table (0x1A4114, 20 bytes a place:
    left, right, up, down) to the place pointed at (the highlight elements of
    0x1A4348 as the places' shapes), a way that does not move learnt as shut.
    The character select (the same screen, drawn from 0x76F37) steps the
    ring left / right per click, one-frame presses, queued.
  - xboxrecomp: xbox_pad_filter (a title's changes to the pad state),
    xbox_mouse_buttons_taken (the mouse buttons out of the bindings),
    xbox_InputMouseButtons, xbox_InputGameInFront, xbox_FramebufferMouse
    (where, as fractions of the window), xbox_fb_clicks (clicks counted by
    the window), xbox_fb_hide_cursor (the system pointer hidden over the
    window), xbox_PresentMap (where the picture went, both renderers), and
    the F10 option GFX_MOUSE ("Mouse pointer in menus", [Input] menu_mouse,
    TT_MENU_MOUSE). Testing: TT_MENULOG (the elements and text drawn),
    TT_MOUSELOG, TT_MOUSE_SCRIPT ("ms x y buttons" lines), TT_NOEXIT.
1.9 (later the same day):
  - Mission select: the place pointed at is set at once (+0x10 of 0x4AD058,
    then 0x00076440 and the move's sounds 0xAB4/0xAB5 or 0xAB8/0xAB9, as the
    game's move at 0x000776EC does), any place the game's neighbour table
    reaches; a place's icon counts as well as its highlight.
  - Leaves (Back 96x75 at 42,373, word at 11,419; Continue at 484,373 /
    515,419, when its word is not empty): only the leaf (less its square's
    margins) and the word are hit; lit under the pointer (the word drawn in
    0xFF4DDAF4 for that draw: tt_menu_el / tt_menu_el_done round 0x000C8940).
    Character select: B / A. Upgrade screens: the game's choice taken there
    (left / right from the list, up back to it; down from the level leaves),
    then A.
  - Upgrade screens: the level leaves (128x64 at y 90) and their mark
    (y 95, on while they are chosen; its x + 19 a leaf's): up to them, then
    left / right.
  - The pointer is shown from the first menu; in play the system pointer is
    hidden over the window too.
  - Mouse aiming: while the ranged button (left trigger) is held in play,
    the mouse's movement (xbox_FramebufferMouseDelta: the system pointer put
    back in the window's middle on Windows, SDL's relative mode elsewhere)
    pushes a stick that replaces the left stick, |stick| <= 1, 160 pixels
    to full at normal speed. Options GFX_MOUSE_SENS (0.5..2, also the
    pointer's own speed: at other than 1 the pointer moves by the mouse's
    movement), GFX_CURSOR_SIZE, GFX_MOUSE_AIM, GFX_MOUSE_INVERT; [Input]
    mouse_sensitivity, cursor_size, mouse_aim, mouse_invert.
  - In a wide picture (play: the pause screen's upgrades; a front end that
    draws widened) the 2D is narrowed about the middle or kept to an edge,
    as the renderer grouped it: hit testing asks how (xbox_HudWidePlace) and
    maps the pointer for each item accordingly (the Back leaf and the level
    leaves sit at the edge groups and were missed).
  - A hovered leaf is lit as the game lights the chosen one: the word in
    0xFF4DDAF4 and the leaf's picture (+0xB8) the lit one (seen from the
    game lighting it, else the dim one's + 0x20).
  - Launcher: the graphics settings in two columns (it had grown taller than
    the screen).
  - Options pages (twotowers/src/options.c): the Options page's drawing
    (0x0007A4D0) is replaced by pages of ours while one is up -- a first
    page (General / Display / Controls) and the Display and Controls pages,
    built from copies of the page's own elements (names 0x434FEC, values
    0x435514, markers 0x433EBC/0x433F98 with the flower or ring pictures,
    background 0x4338B8's picture + 0xA0/+0x80 and +0x60/+0x40, slider bar
    0x433B4C) in memory of ours; their settings through the F10 menu's
    (xbox_GfxItemCount / xbox_GfxIndex / xbox_GfxSetIndex). While ours is up
    the game's page sees a controller at rest; B on the General page comes
    back to the first page and is held from the game until let go.
  - Title screen: "Press START button" as "Press <Start's key>" when the
    keyboard is played (Button prompts keyboard, or automatic and the keys
    used last). F10 menu: the system's pointer shown over it (even in full
    screen: xbox_fb_hide_cursor -1), ours put away. The pointer over the
    films before the first level.
  - Mission select: Secret Codes (place 27) pointed at and chosen; the
    picture galleries step left / right with a click on either half.

  - The hovered leaf's lit picture: the dim one's + 0x20 is guessed only
    on the character select and the upgrade screens, and the picture is
    swapped at the draw only while the leaf still shows the dim one it was
    taken from (after a level, a leaf being torn down held freed memory,
    0xF5F5F601; its "+ 0x20" crashed 0x000C8940). Pictures learned per
    screen.
  - Press confirmation by the game's own polls and time (a press is held
    until the game has read it twice, at most 250 ms), for the options pages
    at unlocked frame rates.
twotowers/src/heroes.c (Extra heroes)
  - Sauron's sprint again (gone since 1.7.4): the left stick's click
    (Shift; both rebindable), "sprint" toggle (default) / hold / off in
    [Input], F10, the launcher and the Controls page. Nothing about his
    moves is changed (1.7.2 swapped his run record, and a press made him
    skip and jump): each frame his own step is lengthened up to 1.6x,
    easing in and out, while he runs under the stick at his running speed
    (a wall, a stop, a lunge, a scene: the game's step alone). Toggle ends
    when he stands still 0.4 s. TT_SPRINTLOG, TT_SPRINTNOGATE testing.
  - A new hero's weapon swap (the sword key 13F82514) only on records that
    are his (wop_is_him: his mesh key in the record or its actor); other
    actors in scenes keep their own (an archer held Sauron's mace in the
    Plains of Rohan's opening). TT_SWORD2ALL the old way, TT_WOPLOG.
xinput_device.c: pad scripts take L3.
  - Options pages: the pointer chooses and clicks our rows itself
    (tt_opt_mouse in options.c: the first page's three, the Display and
    Controls rows, their values, arrows and sliders, dragging a knob, the
    "..." to scroll). menus.c's way -- walking the chosen one with up / down
    -- went back and forth between two rows at a high frame rate, and a
    click on General Options could open it and leave it at once.
  - Walking a list: no next step until the game shows the last one (or a
    quarter second): at a high frame rate the old choice was still seen and
    the pointer pressed again (back and forth).
  - The game's lists under the pointer: the last drawn first. The "Are you
    finished purchasing upgrades?" dialog's Yes / No was taken for the
    upgrades' list behind it, and the pointer pressed up over and over to
    go "back to the list" (it turned Yes / No over).
  - The lit leaf: a lit picture learned is forgotten when the dim one
    changes, and a picture is used only if its descriptor is in the game's
    memory, next to the dim one and of its kind. (Reproduced under Wine: the
    results screen's lit Continue, learned, was used on the upgrades screen
    after it -- the same screen number -- and crashed at 0x000C8940.)
  - The Options pages "closed" and reset to General Options many times a
    second on Windows: the page's last-drawn stamp is GetTickCount() | 1, a
    millisecond ahead of the clock while it reads the same (15.6 ms at a
    time on Windows), and "now - stamp" unsigned was four billion. Compared
    signed now (options.c, and the ranged button's stamp for mouse aiming in
    menus.c, which dropped out the same way). Neither showed under Wine,
    whose clock moves every millisecond.
  - Internal resolution 4.5x: 2160 lines (3840 x 2160 at 16:9, a 4K screen
    pixel for pixel). The renderers' scale is a float now (g_rsf; the
    targets' sizes were already rounded from float scales); render_scale=4.5
    in the ini, F10, the launcher and the Display page ("2160p");
    xbox_GfxScaleF.
  - Mouse wheel: xbox_fb_wheel counts notches (fb_present.c: WM_MOUSEWHEEL,
    partial notches added up; SDL_MOUSEWHEEL). Our options pages scroll
    their rows by it (the first page: its choice); in the game's own menus
    with a list it is up / down, a step a notch (menus.c). Mouse scripts:
    buttons 8 / 16 a notch down / up.
  - Controls page: the key bindings after the settings, a row per action
    (input_bind.h's), its keys and, for a button, the controller's button.
    A / a click on the keys: the next key or mouse button pressed is bound
    (Esc cancels, Delete clears); left / right or a click on the button:
    the next controller button. "Reset Controls" at the end. Saved to the
    ini as the F10 menu's are.
  - xinput_device.c: a key held by the window's messages but up on the
    keyboard was let go at once half the time instead of after 250 ms (the
    same unsigned stamp | 1 comparison); signed now.
  - Results screen after a level: a click on Replay walks the game's choice
    to it (left) and presses A (it pressed B, which is not Replay there).
  - The upgrades after a level: the level leaves have no marker there; the
    chosen one is told by its picture (apart from the others' run of
    pictures), and up is pressed first while the list or a leaf is chosen.
  - prompts_kb.inc: the menus' messages name the bound key or button too
    ("Game Saved.  Press the A button to continue.", "Unable to save / load
    game...", "No saved games...", "Not unlocked yet...", the upgrade
    points message, "Press START to resume"), shorter forms when the key
    names do not fit ("K / Left Click", then the key alone); written again
    every 2 s; a place is written to only while it still holds the game's
    words or ours (it was told by its first letter).
  - Sauron's sprint did nothing on a real PC: it waited for him to run "at
    his usual speed", a speed taken as the fastest seen and only slowly
    let go -- one fast moment (a scene, a knock) and it stayed off. Now it
    is on while he moves under the pushed stick (the extra is in proportion
    to his own step: against a wall, next to nothing). The left stick click
    is taken over every pad read since the last frame (a controller and
    the keys as two pads would otherwise hide each other's). The log says
    when the button is seen and why the sprint is off ([SPRINT]).
  - Fall-through catch: a third fall within 20 s puts him back where he came
    into play (the place he stood was dropping him again and again).
  - Testing: RECOMP_TSC_SCALE=f (RECOMP_TSC_SCALE_AFTER=s): the guest's
    clock at f times real time from s seconds on -- a slow machine's frames
    short to the game, as on a fast PC. TT_POSPUSH (heroes.c): do writes to
    the player's place hold (they do).
  - fps=unlocked: 60 in the in-engine scenes (play's screen, no film, no HUD
    drawn for 300 ms: tt_in_scene) and for 5 s of play after one. At 165
    fps the scenes' characters did not turn their heads (the prologue's
    ending) and Sauron dropped through the ground as the Gates of Moria
    began; at 60 neither (the player's test). TT_SCENE60=0: off.
  - The hero with no bow (Sauron, Gandalf, Frodo, the Uruk-hai) draws
    nothing in the draw hand in a scene: the Plains of Rohan's opening has
    the hero shoot, and Sauron drew his mace back as an arrow. The bow and
    the swords' swaps also ask whose the record is (wop_is_him).
  - The menus' messages "... Press the A button to continue." name Start's
    key with the keyboard (Enter), and while one is up Start is A
    (menus.c msg_scan).
  - The mouse buttons in the menus: the pointer keeps the left (a click)
    and the right (back) -- but on the mission and character select, a
    right button bound to an action is that action (Help, Options, as the
    prompts show); the middle and side buttons keep their bindings
    (xbox_mouse_buttons_taken may now be a mask).
  - The scenes' arrow for a hero with no bow: nothing (restored after a
    test: the level's archers draw the hero's arrow -- the Plains of
    Rohan's opening, on the wall -- and drew Sauron's mace). Heavy attacks
    measured unchanged against 1.8 (Balin's Tomb, Sauron: 9 hits in 91
    sweeps, 1.8 6 in 67). TT_NOSCENEARROW: as before.
  - A "Press A to continue" message up (with keyboard prompts): the A
    button's picture is Start's key (Enter), which closes it as A does
    (the xbe's messages draw the button as a picture, not words).
  - Right click on the mission / character select with the right button
    bound to an action (Help, Options): that action's button is pressed by
    the pointer (through the bindings it did not reach the game).
  - Options: Back (Tab, Backspace) backs out of the Options pages as B.
  - Internal resolution 5x, 6x, 7x and 8x (F10, launcher, Display page).
  - The films before the title (the logos): a left click skips them. The
    game plays them to the end whatever is pressed; its film loop
    (0x000E8B70) stops when Bink's frame (HBINK +0xC) is frames - 1, so the
    movie (found in memory: 640 x 480, its frame count and rate; in low
    memory, 0x011AECE0 here) is put there. Only until the title has shown.
    Testing: TT_FILMCLICK (a click every 4 s), TT_BINKDUMP.

The work above from "1.9: the main menu's Exit and the mouse pointer in the
menus" on was released as 1.8, replacing the earlier 1.8 (which had 1.7.1 to
1.7.8); the internal 1.9 / 1.9.1 labels above are those builds'.

-- 1.8.1 ------------------------------------------------------------------
  - fps=unlocked is unlocked throughout again (the 60 fps scenes and
    5 s after them, 1.8, are gone: choose 60 before them instead).
  - The pointer's presses are held, and let go, at least 70 ms each: at a
    high frame rate a press was a few milliseconds, and the end-of-level
    screens took no left / right that short -- Replay, Continue and the level
    leaves answered only when already chosen.
  - The picture galleries (Concept Art, Production Photos: front-end screen
    11, one 640 x 448 picture drawn by 0x0007F3D7): a click on the left half
    is the previous picture, on the right half the next (the map's click
    handling took them before, and chose another place on the map).
  - Window size in the Display Options page and F10 (GFX_WINSIZE,
    [Display] window_size; xbox_FramebufferSetWindowSize, Win32 and SDL; in
    full screen, the size it comes back to). GFX_E3HUD / E3PICKUPS move on.
  - Mouse steering (GFX_MOUSE_STEER, [Input] mouse_steer, off by default):
    in play the mouse's left / right turns the way the left stick and the
    move keys go (W goes where the mouse has turned to). TT_STEERTEST.
  - Frodo's and the Uruk-hai's thrown stones are the game's own shots at the
    Gates of Moria (the Watcher took no harm from projectile 10).

-- 1.8.2 ------------------------------------------------------------------
  - The end-of-level upgrade screen's level leaves follow the pointer: with
    no marker there and the chosen leaf's glow not one of the pictures, the
    leaf the game has chosen is read from the game (0x4B7054, 0 the
    leftmost); hover goes up to the leaves and left / right to the one
    under the pointer, a click the same. TT_LVL_DONE=n (testing: the first n
    levels finished by every hero, in memory).
  - Mouse steering turns the hero where he stands, as aiming the ranged
    attack does (his facing +0xE8 / +0x158 and his controller's record,
    0x46D450 on, +0x2C), and the move keys go by the way he faces: W ahead
    of him, S back, A / D to his sides. The camera's way for the move keys
    is learned from him as he walks (heroes.c steer_frame). TT_STEERLOG,
    TT_STEERTURN=a,b.
  - Frodo's stone at the Gates of Moria is the stone again (its look and
    flight), but what it strikes is struck as by his own shot (the
    projectile object's +0x3D0 set back after the launch). TT_NOFRODOSWAP.
  - Ultra textures (GFX_ULTRA, [Display] ultra_textures, the Display Options
    page and the launcher; off by default): the GameCube release's
    high-resolution texture pack (Dolphin's GLOS69 folder, beside the game)
    drawn in place of the game's own textures. Its names hash the GameCube's
    texture data, so textures are paired by their look: textures_ultra\
    index.bin (textures_ultra\tools\make_index.py) holds a 16 x 16 average of
    each picture, and every texture the game decodes takes the nearest one of
    its shape when near enough and clearly nearer than any other
    (kernel/nv2a_ultra.inc). The pack's BC7 DDS files are read on a thread of
    their own and drawn with all their levels (Direct3D 11; OpenGL with BPTC,
    not macOS); its PNG pictures are made BC7 DDS in textures_ultra\png
    (tools\png2dds, bc7enc). Within RECOMP_ULTRA_MB (1536) of graphics
    memory. The green health pickup is left as it is (RECOMP_ULTRA_SKIP, set
    from the orange pickups mod's list; the pack's picture of it is left out
    of the index). RECOMP_ULTRA_LOG logs the pairings.
  - Ultra textures: textures_ultra\pairs.txt -- pairings checked by eye
    ("HASH path", or "HASH -" to keep a texture as it is), over the
    automatic ones: every texture of a full texture dump the automatic
    pairing passed over, beside its three nearest pictures, and every picture
    no texture took, beside its three nearest textures, looked through.
  - Ultra textures: the game's own textures the pack has no picture for --
    menus, text and fonts, icons, the level select (its map and pictures),
    button prompts, effects -- upscaled 4x (Real-ESRGAN general-x4v3, colour
    and alpha apart; soft glows plain Lanczos) into textures_ultra\ui\
    <hash>.dds, paired by hash in pairs.txt (make_index.py indexes them with
    a shape no texture has, so they are never paired by look). A texture a
    mod changes (the E3 HUD's, the orange pickups) keeps the mod's version.
  - E3 HUD: the portrait medallion is made at 256 px (was 128) and the gauge
    and rune orb at 8x (was 4x), and the portrait icons are upscaled 4x
    (originals kept in e3hud\original): sharper at high internal resolutions.
  - Pointer: in the whole front end the mouse buttons are the pointer's
    only (the title and the main menu play a film behind them, which made
    them "not a menu": a click went to the game as A -- the title passed by
    itself, the chosen item, the Credits, was pressed); a front-end screen
    with something to choose is a menu even over a film; a click skips only
    the intro films (screen 8). The right button over the Options pages
    opened from the mission select is Back (as the select's own action,
    Options, it opened them again at once).
  - Ultra textures: black boxes -- GameCube pictures keep their transparency
    apart, so many are opaque where the game's texture is see-through. Those
    found are made again with the game texture's transparency
    (textures_ultra\alphafix, tools\alphafix.py, check_alpha.py); any other
    such pairing is refused at run time and the game's own texture drawn.
  - Uruk: his burning rock carries the fire pots' own trail (the game ties it
    to each shot) instead of effects made each frame (the effect is one per
    id: the archers' pots' fire went out while his rock flew, and his showed
    little). TT_ROCKOLDFIRE: the old way.
  - The right button on the character select is Back (unselects the hero
    chosen), not its bound action.
  - Ultra textures: a pack picture drawn as the sky (clamped, depth tested
    and not written, 256 or wider) is kept out and the game's sky drawn: the
    pack's skies are laid out otherwise and the box showed its corners.
    RECOMP_ULTRA_SKY keeps them. The upscaled cut-out textures (grass,
    foliage, fences) are made again with their colour carried under the
    transparent texels and a crisp edge: they had dark outlines.
  - Effects of ours (Gandalf's / Sauron's beam glows, the rock's embers) take
    only the game's free particle effects above a reserve of 20: the game
    keeps 64 (0x43AD1C) and, full, ended the archers' fire pots' flames.
  - The in-engine scenes stay 4:3 in widescreen (RECOMP_WIDE_SCENES=1 widens
    them): their cameras showed the skybox's corners and the mountain cards'
    edges, built for a 4:3 view.
  - Ultra textures: cut-out pack pictures remade with their colour under the
    transparent texels (textures_ultra\bleed, bleed.py; png2dds does it to
    every picture it converts): the pack's are dark there, and filtering and
    the smaller levels gave grass and leaves dark rims.
  - The beams are drawn whole again (the reserve kept for the game's
    effects applies to the rock's embers only).
  - Widescreen cutscenes (GFX_WIDE_SCENES, [Display] wide_scenes, Display
    Options and the launcher; off: 4:3).
  - e3hud.c's option numbers follow gfx_menu.h again (Ultra textures had
    moved them: the E3 HUD and orange pickups read the wrong options).
  - Particle effects: the game's pool of 64 (0x0008E270) is grown to 512
    (RECOMP_FX_MAX, 64-2048) when a level makes it, the extra emitters made
    the game's way in an arena of ours given to its allocator (0x000E6B50)
    for the moment; 0x0008E470 takes them from the free list past 64. The
    beams, the rock's fire and the archers' fire pots all play at once.

-- 1.9 ------------------------------------------------------------------

twotowers/tools/ultra_textures/ultra_build.h (new), bc7decomp.cpp/.h (new),
setup_launcher.cpp, linux_setup.cpp, linux/TwoTowers.sh,
macos/TwoTowers.command, CMakeLists.txt
  - Ultra textures without the GameCube pack in the download: the pack
    (GLOS69) is its author's and is downloaded separately. What ships is
    ours: textures_ultra\ui (the game's menus, level select, text and HUD,
    upscaled), masks\ (the game textures' transparency, for the alphafix
    pictures), recipe.txt (the pictures remade: 219 bled cut-outs, 101
    alphafix), pairs.txt, and an index of the ui pictures alone.
  - ultra_build.h makes the rest from the pack, as the Python tools did:
    the PNG pictures to BC7 DDS (colour carried under transparent texels),
    the cut-outs bled, the alphafix pictures (the mask scaled bicubic to the
    picture), and index.bin (signatures as make_index.py; a PNG's from its
    own colours). Threads share each picture's blocks; an interrupted run
    keeps what it finished; built.txt records the pack (count, sizes): a
    different pack is made again. Checked against the hand-made set on the
    real pack: the same 2869 pictures, the pack's signatures equal, PNG
    ones within 6, pictures within BC7's noise; 4.5 minutes on 2 cores.
  - The launcher builds it before Play (progress bar), --build-ultra builds
    it and closes (the game starts it); twotowers-setup ultra /
    ultra-needed for Linux and macOS (the start scripts run it).

xboxrecomp-0.12.0/src/kernel/nv2a_ultra.inc, nv2a_d3d11.inc, nv2a_gl.inc
  - Ultra textures turned on in the game with the pack there but not made
    ready: RECOMP_ULTRA_BUILDER (set by twotowers_host.h: the launcher with
    --build-ultra, or twotowers-setup ultra) is started, and the pack's
    pictures are added when textures_ultra\built.txt appears (the index is
    made with room for 8192 pictures; the shipped ones come first, so their
    places do not move). Pairings of pack pictures match by file name, so
    GLOS69\GLOS69 works too.

xboxrecomp-0.12.0/src/video/gfx_menu.c/.h, options.c, setup_launcher.cpp,
twotowers_host.h, nv2a_d3d11.inc, nv2a_gl.inc, nv2a_d3d11_shaders.h
  - Internal resolution up to 12x. Frame-sized targets are held within
    16384 (D3D11; GL_MAX_TEXTURE_SIZE on GL): 12x at 32:9 would be 20480
    wide, and both scales come down together.
  - The picture is averaged down to the window: up to 8 x 8 bilinear taps
    over the texels a window pixel covers (ds_fetch), sharpening at window
    pixels. One bilinear tap left most texels of a 4x+ frame unseen.
  - MSAA 2x / 4x / 8x (Anti-aliasing: msaa2, msaa4, msaa8, msaa4+fxaa,
    msaa8+fxaa). Every colour and zeta surface gets a multisampled copy that
    drawing goes to (D3D11: a texture, GL: a renderbuffer); the plain
    texture is made from it before anything reads it (ResolveSubresource /
    a blit; zeta: one sample per texel written as depth by ps_zres), and
    what is written to the plain texture another way (CPU uploads, copies)
    is drawn into the multisampled copy before the next draw. Fewer samples
    where colour and zeta would pass 1.5 GB; a surface that cannot be made
    turns MSAA off. Tested under Wine (D3D11) and on Linux (GL): edges
    smoothed, effects and scenes as before.

-- 1.9.1 ----------------------------------------------------------------

twotowers/src/menus.c, gfx_menu.c, setup_launcher.cpp, twotowers_host.h
  - Mouse steering is the prologue's aiming-with-no-bow: while forward is
    held (W, the stick pushed up past 12000) the left stick is the mouse's
    -- pushed by its movement, kept where pushed, at most full -- so the
    hero runs the way the mouse points from the camera's view, and stops
    when forward is let go. It starts pointing ahead. The 1.8.2 turning in
    place (heroes.c steer_frame) is off (tt_steer_active 0).
twotowers/src/heroes.c
  - The Uruk-hai at the Gates of Moria (02GATE*): his rock (and the fire
    rock, the pot) again, not the bow's arrows; struck as the arrow he had
    (s_frodo_pid, as Frodo's stone), the swap made after the fire rock's
    entry is set (it looks for projectile 10).
twotowers/src/heroes.c, options.c, setup_launcher.cpp, twotowers_host.h,
xboxrecomp-0.12.0/src/video/gfx_menu.c
  - Sauron's sprint removed: sprint_frame no longer runs; its option is gone
    from the Controls page, F10 and the launcher (GFX_SPRINT keeps its
    number, unshown -- row_item skips it -- so the options after it keep
    theirs), and from the ini written for new installs.


-- 1.9.2 ----------------------------------------------------------------

twotowers/src/lang.c (new), tools/lang/ (new), lang/ (new), main.c,
heroes.c, twotowers_manual.c
  - Languages. [Language] language=<code> (TT_LANG): lang/<code>/ holds the
    translations (text.txt Unicode, game.txt in the game's character codes,
    keyed G<FNV-1a of the English UTF-16> / P:<English> / L:<English>, so
    no game text ships) and glyphs.bin (letters the game's four XF fonts
    lack, per font: kind 0 a drawn cell, 1 the game's letter plus marks
    placed above / below / over / beside it, 2 an advance). The game's
    files with text -- UI\startup, credits, mainmenu and the levels'
    Worlds\*.bf? -- are opened as copies (xbox_path_redirect, after the
    Extra heroes' own redirect) made once into lang_cache: the OBSS string
    tables and OBST subtitle tables rebuilt with the language's strings
    (48 units of room after each, for the hints rewritten in place), each
    XF font given a second glyph range from 0xE000 with the cells packed
    below its own in a larger texture (unswizzled, extended, swizzled
    again; header size and format word updated), only the letters that
    file's strings use (startup: all of them). Resources are appended
    page-aligned and the resource table rewritten at the end.
  - startup.BFF is loaded into a fixed 0xD4800-byte area (0x000D3990, this
    0x4ACCB0, slot 1): grown to the copy's size when it is larger.
  - Right-to-left lines (Hebrew, Arabic) are turned for drawing in the line
    drawer (0x0003F210): the line's units reversed into a scratch buffer,
    runs of digits and Latin kept in order, brackets mirrored.
  - Chinese, Japanese and Korean: a zero-wide 0x20 between characters lets
    the game's word wrap break lines there (spaces become 0xA0); Thai the
    same at U+200B. Arabic in its joined presentation forms; Devanagari a
    cell per syllable.
  - The port's own words: the Options pages (options.c word()), Exit and
    "Press <key>" (menus.c), the loading-screen and message hints that
    name keys (prompts_kb.inc, now built in game units: the language's
    hint found in memory by its words and rewritten within its room), the
    Extra heroes' names and upgrades (heroes_lang.inc: the English string's
    key, the hero, the key of his version -- written whole in the
    language), the ECTS HUD's tip; the F10 panel through
    xbox_gfxmenu_tr / _face / _rtl (DrawTextW; POSIX host_text falls back
    to other fonts for letters its font lacks).
setup_launcher.cpp, linux_setup.cpp, twotowers_host.h
  - The launcher's Language list (lang\*\info.txt): writes
    [Language] language and starts the launcher again in it; every
    launcher word through lang\<code>\text.txt (L: keys), a font with the
    language's letters for CJK, Thai and Hindi, right to left for Hebrew
    and Arabic. twotowers-setup: "l) Language". The ini template has the
    [Language] section.
twotowers/src/menus.c, gfx_menu.c, setup_launcher.cpp, twotowers_host.h
  - Mouse steering stays 1.9.1's (hold W, the mouse is the stick), scaled
    by forward's own push: Walk (W part way) walks while steering. Before,
    steering needed the stick past 12000 and took the mouse's whole stick.
textures_ultra/pairs.txt (tools/ultra_textures)
  - Legolas's results picture (86216F9D) was paired by review with the
    pack's tex1_640x512_1beb0dacb70603e0 -- a second picture of Aragorn
    (the pack has none of Legolas): with Ultra textures his results page
    showed Aragorn. It is paired with the upscaled picture of his own
    (ui/8F94C8D5).
twotowers/src/twotowers_manual.c
  - The mission results page (0x00071F60) loads endlvl.BFF and sets the
    hero's picture (0x426BD0 from the table at 0x4AB580) and figures
    (0x00071970) only when entered from play (screen 6); entered another
    way after a level it would keep the last mission's. The first entry
    after a level is now set up as from play (a precaution, found while
    looking for the Legolas picture above).
twotowers/tools/ultra_textures/ultra_build.h, setup_launcher.cpp
  - Ultra textures were prepared again and again on Play: built.txt was
    written in text mode (CRLF on Windows) and compared byte for byte with
    the stamp (LF), so the pack always looked unprepared, and Play after a
    build built once more. built.txt is written in binary and read
    ignoring CR (existing files are accepted), and the launcher builds at
    most once per Play.
  - The launcher's status while preparing the pack, its errors, and the
    messages made of several literals are translated too.
twotowers/src/lang.c, lang_ultra.cpp (new), tools/lang/build_lang.py,
xboxrecomp-0.12.0/src/kernel/nv2a_ultra.inc
  - Ultra textures' upscaled text in every language: glyphs4.bin holds the
    added letters drawn 4 times the size (zlib), and as a font is copied
    with a language's letters, its upscaled picture is made too -- the
    game's own (textures_ultra/ui/<hash of the original>.dds) with the new
    letters put where the font's are, accented letters from its own
    upscaled letter and marks -- once, into lang_cache\<code>\ultra
    (BC7, the launcher's encoder), and paired with the new texture's hash
    (xbox_UltraAddPair: the index and pairs gain the title's pictures).
    The fonts a copy pairs are listed beside it (<copy>.ultra) and paired
    again whenever the copy is used again: from the second start on, the
    copies were reused and their upscaled fonts were never paired, so the
    main menu and level select drew the language's text unscaled. The
    title's own pictures also skip the check against GameCube pictures
    with the wrong transparency (they carry no signature to check).
  - A language's font textures grow wider rather than taller, up to 2048
    a side: the renderer takes no texture over 2048, and a 256 x 4096 font
    (Chinese, Japanese) drew as a black box with Direct3D 11.
twotowers/CMakeLists.txt
  - The game has C++ in it now (lang_ultra.cpp): on Linux the C++ runtime
    is linked in (-static-libstdc++ -static-libgcc), as twotowers-setup's,
    so the build still runs on glibc 2.27 systems without a newer libstdc++.


-- 1.9.3 ----------------------------------------------------------------

xboxrecomp-0.12.0/src/kernel/nv2a_d3d11.inc, nv2a_d3d11_shaders.h
  - Character shadows without the pale streaks. The game draws them as
    stencil volumes (faces counted into the stencil, alpha only), then two
    full-screen passes darken where the count is not zero. Its CPU-built
    volume is not closed everywhere, so thin lines inside a shadow keep a
    count of 0 and stay lit. Just before the first darkening pass the
    stencil's "not zero" is made a mask (from its samples with MSAA),
    reduced to 1/d of the size (each pixel the largest of its d x d; d
    about half the internal resolution's scale), closed there (dilated and
    eroded, one axis at a time, gaps up to 4 pixels at 640x480), and the
    stencil set (and the shadow's strength put in the alpha) where the
    closed mask covers a pixel the shadow missed. The game's passes then
    darken as before. RECOMP_SHADOW_R=0 leaves them as drawn; TT_SHLOG
    logs the repairs (testing).
xboxrecomp-0.12.0/src/video/fb_present.c, gfx_menu.c/.h, twotowers/src/
options.c, setup_launcher.cpp, twotowers_host.h
  - Exclusive full screen: [Display] fullscreen=2 and fs_resolution=WxH
    (empty: the desktop's). The display is switched with
    ChangeDisplaySettingsEx at the mode's highest refresh rate, on the
    render thread between two frames (xbox_FramebufferModeService; a
    change from the window thread could meet a Present in flight), and the
    borderless window covers it; leaving the game puts the desktop's mode
    back and minimises, coming back switches again. Display mode in the
    Options page, F10 and the launcher has Windowed / Borderless /
    Exclusive, and Full-screen resolution lists the display's own modes
    (the screen's own, the common ones it offers, the largest of the rest).
  - Every new display mode (WM_DISPLAYCHANGE, and after our own change) the
    full-screen window covers it again; the mouse's relative movement is
    measured from the middle of the part of the window on its screen. A
    window left larger than a new mode had its middle off the screen,
    where Windows will not put the pointer: the pointer drifted, and the
    menus' choice followed it (W A S D seemed not to work).
  - The Options page stays on the page shown while a new resolution holds
    the picture (frames gone by, not 400 ms).
twotowers/src/menus.c, options.c, xboxrecomp-0.12.0/src/input/xinput_device.c
  - In the front end (not play or films) the left stick pushed past about
    a quarter is pushed all the way along its main direction, so the
    game's menus move as with the d-pad: the main menu moved only for a
    near-full push (a 40% push did nothing), and a push a little to the
    side lost more. A held stick steps at the d-pad's pace.
  - Mouse steering only when the left stick is the keys' (W), and mouse
    aiming only for a key's or mouse button's ranged button
    (xbox_input_kb_lstick / xbox_input_kb_lt from the input layer): a
    controller's stick pushed up was turned the mouse's way, and its LT
    gave the stick to the mouse.
  - The Options pages read what the pad's last read holds as well as what
    was pressed since the last frame: at a high frame rate most frames see
    no read, and every read was a new press -- a held stick ran down the
    page.
twotowers/src/prompts.inc, prompts_kb.inc
  - The Secret Codes page: its L trigger picture (0xDBA098CA) follows the
    prompt style as R does; "Press the START button to pause" and "Pull and
    hold" read as the keys' with keyboard prompts.
twotowers/src/e3hud.c, xboxrecomp-0.12.0/src/video/gfx_menu.c
  - The ECTS HUD finds its two settings by their ini key (xbox_GfxFind),
    not their number: a setting added before them moved the numbers, and
    the orange health pickups stayed green.


-- 1.9.4 ----------------------------------------------------------------

xboxrecomp-0.12.0/src/kernel/nv2a_pb_scan.c, nv2a_pb_exec.c, xbox_memory_layout.c
  - GPU commands are recorded as the title kicks them (following JUMP /
    CALL / RETURN at once) into a ring, and executed by a render thread of
    their own (Windows, Direct3D 11; RECOMP_GPU_SYNC=1: as before). The
    title's fences complete when the render thread reaches them;
    xbox_Nv2aDrained (GET == PUT and the ring empty) is the "GPU idle".
  - Runs of INLINE_ARRAY words go into the ring as one record and to the
    executor in one piece; the method inventory only when surveying; the
    ring's free space tracked by the producer; events signalled only to a
    waiting thread; unhandled methods counted through a hash.
  - frame_limit: the frame-rate limit where frames go out, so the menus
    (which draw without the game's frame wait) keep it too.
xboxrecomp-0.12.0/src/kernel/kernel_bridge.c, kernel_thread.c
  - Every queued D3D callback runs at each timer tick; a guest thread that
    waits on the GPU with one queued wakes the timer thread at once
    (RECOMP_CB_GRID=1: on the 2 ms tick only).
xboxrecomp-0.12.0/src/kernel/nv2a_d3d11.inc, nv2a_sw3d.inc
  - Pipeline state (layout, buffers, shaders, constant buffers, state
    objects) sent only when it changes; the vertex-program path's vertices
    and indices share one ring buffer, one Map a draw.
  - Texture binds remembered while nothing could change them; swizzled
    textures (told by format, not pitch) checked at most every 8 ms, linear
    ones every kick; the texture caches hold 2048 (were 256 / 512 -- a busy
    scene with an extra hero's bolt used over 300 textures a frame and
    decoded them all again every frame: 20 fps), found through indexes.
  - Present allows tearing with vsync off; on Vulkan (DXVK) frames go out
    at once through DXVK's mailbox mode (DXVK_CONFIG dxvk.tearFree = True;
    RECOMP_VULKAN_SYNC=1: on the refresh, as before).
  - Log lines: [PERF] (where a frame's CPU time goes), [PERFD3D] (Direct3D
    calls a frame), [SLOWFRAME] (a frame the render thread spent over
    25 ms on, broken down).
twotowers/src/twotowers_manual.c
  - The game's frame time is handed out in whole milliseconds with the
    remainder carried (sub_000E3350): the world update truncates it to
    whole ms, and at 400 fps lost a fifth of every frame -- everything ran
    slow and felt delayed above the monitor's rate. [FRAMETIME] log line.
  - Frame rate 30, 60, 75, 90, 100, 120, 144, 165, 200, 240, 300 or
    unlocked (at most 1000); the limits pace the game's frame wait, and
    the frame-wait / flush watchdogs give a lost GPU signal (the "Load
    game" black screen).
xboxrecomp-0.12.0/src/video/gfx_menu.c, twotowers/src/setup_launcher.cpp
  - The frame-rate list; exclusive full-screen resolutions smallest first
    (the right arrow goes higher, as Window size).
twotowers/src/d3d_native.inc (1.9.5), xboxrecomp-0.12.0/tools/recomp/manual_scan.py
  - The game's hottest Direct3D library functions written in C instead of
    lifted: DrawIndexedVerticesUP (0x118300), DrawIndexedVertices
    (0x1184E0), DrawVertices (0x118440), the vertex stream setup before a
    draw (0x120370), one vertex shader constant (0x116A40), the dword copy
    (0x118110). Same pushbuffer, byte for byte; the library's share of the
    game thread's time about halved. TT_D3D_NATIVE=0 the lifted code,
    =2 every call checked against it ([D3D] log lines).
  - The override scan follows a manual file's local #includes.
twotowers/postproc_regs.py (1.9.5), generate.py
  - The guest's integer registers kept in C locals inside each lifted
    function, written to the per-thread globals only at calls, tail jumps
    and returns. The compiler had been storing every register change to
    memory (a volatile guest access is a point it will not move a store
    past). 8682 functions converted, 5 left; a differential test of every
    converted function (old and new body from the same random state:
    registers, memory, x87 stack, the calls made) found them identical,
    and ran them about a quarter faster.
kernel/nv2a_copy.h, nv2a_sw3d.inc, nv2a_d3d11.inc, nv2a_pb_exec.c, nv2a_pb_scan.c (1.9.5)
  From a profile of the render thread on a real PC:
  - Vertex data written into the graphics card's mapped ring with SSE2
    streaming stores, and the recorded pushbuffer, inline vertices and
    constants copied with SSE2 (msvcrt's memcpy was the render thread's
    largest single cost, and a large one on the game thread's recording).
  - RECOMP_BASE_VERTEX=1: the gathered draws drawn with a base vertex, the
    vertex ring bound once. Off: on an NVIDIA card the driver's share of the
    render thread grew to about three times (profiled), more than it saved.
  - Blend, depth, raster and sampler states found through a small hash
    table (the samplers of several stages took turns and each miss searched
    the whole list).
  - The [PERFRT] timing (a dozen processor-clock reads a draw, two for every
    constant upload) only with RECOMP_PERFRT=1.
kernel/nv2a_d3d11.inc, nv2a_d3d11_vp.inc, nv2a_pb_exec.c (1.9.5)
  - Draws that differ only in their vertex-program constants (each
    character's bones, each object's place) are joined into one Direct3D
    draw. Every program gets a second shader that reads its constants from
    the vertex ring instead of the constant buffer: the batch's constant
    sets are written after its vertices, then for each vertex where its
    draw's set starts; where that list starts reaches the shader as an
    instance attribute (a buffer of 0, 1, 2, ... read at the draw's first
    instance), so a batch costs no constant-buffer write. About 300 more
    draws a frame joined in a busy scene, no vertex-constant uploads left;
    replays of captured frames identical pixel for pixel.
    RECOMP_CONST_JOIN=0: off. [PERFD3D] counts the joins. Only the
    constants a program reads are copied (up to its highest register; all
    192 when it addresses them relatively).
kernel/nv2a_d3d11.inc, nv2a_pb_exec.c (1.9.5), from the next profile:
  - Textures and samplers set separately, each only for the stages that
    changed.
  - Whether a method changed anything is returned instead of read back
    from s_chg (the byte just or-ed into it made every method's read stall).
  - A surface the CPU may have drawn (movies, loading screens) checked once
    a frame, not once a pushbuffer run (RECOMP_SURF_SYNC_RUN=1: every run).
kernel/nv2a_pb_scan.c, twotowers/src/twotowers_manual.c (1.9.5)
  - Linux: the GPU commands run on a render thread of their own, as on
    Windows (OpenGL's context is taken by the first thread that draws, the
    render thread). macOS keeps the old way until tried on a Mac
    (RECOMP_GPU_ASYNC=1 to try it); RECOMP_GPU_SYNC=1: the old way anywhere.
  - The game's waits for the GPU (D3D's block-on-time, the drain before a
    resource is freed) sleep until the render thread gets further instead
    of yielding in a loop: with the render thread the slower one, the game
    thread had kept a whole core busy (on Linux 14.6 ms of processor time a
    frame, now 3.0). Counted as waiting in [PERF].
twotowers/src/twotowers_manual.c (1.9.5)
  - Animations keep their speed at any frame rate. Each frame an animated
    object's step was whole ms of (frame time x the animation's speed), the
    fraction dropped (0x00028310): at 30 fps under-full-speed animations
    kept 98% of their time, at 500 fps 53% -- half-speed animations above
    300 fps, and a slow layer (heads turning toward a look-at target) could
    stop moving. The fractions are now carried per object; [ANIMSTEP] in the
    log every 10 s shows the original's share against the 100% kept
    (TT_NO_ANIMCARRY=1: as before).
  - Testing hooks keyed to the game's own clock: TT_DUMP_GAME_MS/TT_DUMP_DIR
    (pictures), TT_MEMDUMP_GAME/TT_MEMDUMP_AT (RAM), TT_POSLOG (player
    position), TT_WWATCH/TT_WWATCH_AT (Linux: who writes a word);
    RECOMP_PAD_GAMETIME=1 plays a pad script by game time; RECOMP_GPU_NULL=2
    skips presenting too.
twotowers/src/twotowers_manual.c (1.9.5)
  - Falling at any frame rate (0x000B9530, an actor's gravity step). The
    fall speed had its acceleration added once a frame, so at 500 fps
    anything that stepped off a ledge or jumped built up speed 16 times as
    fast as at 30 fps and landed deep. The add is now scaled to the frame
    time (the original exactly at 30 fps). TT_NO_FALLFIX=1: as before.
  - The same step dropped any move under 0.01 units a frame -- most moves
    above about 300 fps (70% at 1000 fps), so actors stood still or caught
    on the ground. The threshold now scales with the frame time.
  - Testing: TT_FALLLOG=1 ([LAND] each landing, [FALLSTEP] tiny moves),
    TT_SPLOG=1 / j / address (who sets an actor's position, with host call
    stacks on Linux), TT_GSLOG=1 (ground snaps); TT_MEMDUMP_GAME now also
    dumps the 0x80000000 window, where the game's objects live.
apu/apu_vp.c, kernel/nv2a_gl.inc (1.9.5)
  - A voice's gain glides across each 32-sample frame from the last frame's
    instead of stepping (the MCPX ramps its volumes): fewer clicks when a
    title moves its sounds every frame (RECOMP_APU_NORAMP=1: steps).
  - Testing: RECOMP_APU_PIOSTAT, RECOMP_APU_VOLLOG, RECOMP_APU_GSTAT;
    RECOMP_GPU_NULL=3 draws only the frames a picture is asked of
    (TT_DUMP_GAME_MS runs at a fixed frame time, fast).
apu/apu_core.c, apu/apu_vp.c, kernel/nv2a_pb_exec.c (1.9.5)
  - Sound no longer cuts out. Whenever the title held the sound chip's
    front end (DirectSound does around each batch of voice updates), the
    sound thread skipped that 32-sample frame: 3-10% of all sound came out
    as silence, in slices -- crackle, and sounds that seemed to stop part
    way (measured: the same sounds played a third as long unlocked as at 30
    fps). Holding the front end holds back methods only; the voices now
    play on. RECOMP_APU_HOLDSKIP=1: as before. [FPS] counts the frames that
    came round while held.
  - The title's methods are taken under the APU lock, so a voice list never
    changes while the sound thread walks it (RECOMP_APU_NOPIOLOCK=1: not).
  - Testing: RECOMP_APU_LIFELOG=1 ([LIFE] each sound's length).
twotowers/src/twotowers_manual.c (1.9.5)
  - Unlocked (1000 fps cap): a frame is held to at least a millisecond after
    the last. Catching up on a late frame let the next few through back to
    back, under a millisecond apart, and the game's clock could not keep
    such frames straight: the world rounded each one up to a millisecond
    (fistp) while animations cut it to nothing, and frames under half a
    millisecond were left out of the carry altogether. In the Westfold
    intro, Aragorn's run played slow beside the scene. A frame under a
    millisecond (carry short of one) now gives the world 0 ms, and every
    frame's time goes into the carry.
  - Testing: TT_LVLLOG=1 ([LVLRESET]), TT_DUMP_LVL=1 (pictures by the level
    clock).

-- 1.9.6: couch co-op ---------------------------------------------------
Co-op is thanks in part to Volverine-Dude, whose fork (github.com/
Volverine-Dude/TooTawersRecomp-coop, on 1.9.4) is brought together with the
port's own co-op work on 1.9.5; see COOP.md for what it does. The
toolkit changes it makes, and those made bringing it in:

kernel/nv2a_d3d11.inc, nv2a_d3d11_shaders.h (co-op, Volverine-Dude)
  - The head markers' depth probe: the frame's depth read at up to four
    points the title asks for (xbox_zprobe_req / _n), the 24-bit depth
    given back as 0..1 (xbox_zprobe_res, xbox_zprobe_seq counting results).
  - HUD exclusion boxes (xbox_hud_excl, xbox_hud_excl_more): pieces inside
    stay centred and pull no others to the middle; overlay quads kept to the
    left or right edge by their version's bits 31 / 30.
kernel/nv2a_gl.inc (co-op, Volverine-Dude)
  The same edge-pinned overlay quads for OpenGL.
kernel/kernel_bridge.c (co-op, Volverine-Dude)
  Testing: TT_SKIPMOVIES -- a film reported missing (the death film kept).
kernel/nv2a_pb_exec.c (co-op; 1.9.6)
  The depth probe's variables declared where Direct3D 11 isn't built
  (Linux, macOS): no results come, the markers are drawn undimmed.
input/xinput_device.c (1.9.6)
  - Two players' input, set by the title (xbox_input_two_players): each
    player's device chosen (xbox_input_dev_choice: -1 automatic, 0 the
    keyboard and mouse, n the n-th controller found); automatic with one
    controller gives player 1 the keyboard and mouse and player 2 the
    controller. The keyboard and mouse go to the port they are chosen for
    (xbox_InputKeyboardPort), not always port 0.
  - The controllers found and their names (xbox_InputDeviceCount,
    xbox_InputDeviceName), the device a player has (xbox_InputPlayerDevice).
  - Testing: the pad script's right stick (RSU, RSD, RSL, RSR).
twotowers/src/twotowers_manual.c (1.9.6)
  - The co-op's XInputGetState answers player 2's virtual pad and leaves
    the rest to 1.9.5's native controller reading (sub_001697D9_hle); its
    XInputSetState drops rumble for the virtual pad there too.
  - The co-op's ground-snap wrapper calls the TT_GSLOG test log.
  - 64-bit Linux types (GetTickCount, xbox_InputGetState), _strnicmp.
  - Pickups: Double (sub_0008FC60). Testing: TT_COOP_PICKTEST=1.
twotowers/src/options.c (1.9.6)
  - Co-op Options page: Pickups, P1 Device, P2 Device (the Co-op menu keeps
    its five rows); tt_coop_input_sync turns two players' input on in a
    co-op game with a human player 2.
  - The [Coop] settings are read on Linux and macOS (the ini's path was
    built with a backslash).
twotowers/src/menus.c (1.9.6)
  The mouse's steering, aiming and clicks go to the keyboard's player only.
twotowers/src/twotowers_manual.c, apu/apu_vp.c (1.9.6)
  - The upgrade screen's points count down after a purchase at 6000 a
    second at any frame rate (0x0006E830 took 100 off each frame, with the
    purchase sound playing until the count was done: above 60 fps the count
    and its sound were cut short). TT_NO_UPGCOUNTFIX=1: as before.
  - Testing: TT_UPGXP=n (the hero's experience on the upgrade screen),
    RECOMP_APU_OFFBT=<base> (Linux: who stops a sound).

-- 1.9.7: Versus -----------------------------------------------------------
twotowers/src/twotowers_manual.c
  - Versus (tt_vs_on: a co-op game from the main menu's Versus, or
    TT_VERSUS=1 with TT_COOP=clone): the heroes' attack boxes get the mask
    of a character on neither side (0x80008E, sub_000BA020), so each hits
    the other.
  - The arena: the level's records of type 2, 3, 5, 11, 16 (but the
    opening "missionStart" cut-scene), 19 and 29 are not made
    (sub_000F5BC0; TT_VS_OUT=types, TT_VS_KEEPCAST); messages to no one
    are dropped (sub_000C6F60; they hung the game).
  - Rounds (vs_frame): best of three; even health (TT_VS_HP), every move
    bought (sub_0009CAB0), no respawn wait or game over; the banner in the
    respawn counter's element.
  - The P2 Bot and the test bot (TT_COOP_P1SEEK=2) fight the other hero.
  - The new game (0x000D1C10) becomes the match: the arena's level
    (0x1BB2A4), player 1's hero (heroes.c tt_heroes_p1_pick), player 2's.
  - No opening cut-scene (TT_VS_KEEPSTART keeps it): the controls it would
    hand over ([0x43B1B4] = 0) are given at the first frame of play.
  - The countdown (vs_state 3, tt_vs_frozen: both pads zeroed): the round,
    3, 2, 1 (the Options' tick, options.c tt_opt_move_sound) in the rating
    word's big letters, FIGHT (versus/fight.wav through the APU's software
    mixer: versus.c tt_vs_play_wav).
  - A hero at 0 falls (INP_KNOCKDOWN, held at 0 health) and stands at the
    next round's countdown (from 50: 47, INP_RESSURECT).
  - Each hero's target (component +0x98) is the other: the player's world
    pass clears it on everyone outside its lists (0x5A450 -> 0x53B30, now
    skipped for the heroes' components in Versus).
  - Ranged: the game's projectiles pass through a hero of the shooter's
    side, so a shot at the other (the launch, sub_000D9460, [esp+0x18] the
    shooter) is judged by its flight time: a hit if he is within 2.2 units
    of where he stood (TT_VS_ARROWDMG, 45; axes x1.5, bolts x1.33, stones
    x0.67). 10 shots a round (TT_VS_AMMO), one a second (A held back at
    the pad), none on Weathertop and the prologue (LT held back).
  - Pickups: the level's dynamic pickups put out (0x8F410, 0x8F640) each
    round: health in the middle, ammo each side; taken within 1.4 units
    (vs_pk_touch: their own touch never comes here). Double Pickups off.
  - The pause menu without Show Upgrades (its count at 0x4B5808).
  - The results (vs_results_hud): the match's numbers (tt_vs_st) and two
    awards, HUD text over versus/results_bg.bmp (a texture of our own,
    heroes.c tt_vs_image_pixels).
  - Music: the arena's scripts (which start it) are not run, so the
    donor level's track (versus.c; its bank in place of the arena's,
    lang.c) is started at the first FIGHT through the front end's own
    call (0x000C31C0: name, 1 = play, 1, 0); streams of the arena's own
    are renamed to the donor's (sub_000EBF10).
  - Find the Shield (tt_vs_mode 1): a pickup on a walkable pathmap cell
    14-30 units away, no damage until it is taken, the carrier x1.33,
    1 point for beating him / 2 for him, first to 20.
  - The Nazgul's fall (vs_flee): his run away from the other for 1.7 s
    (his matrix turned the way he goes: right +0x60, forward +0x70), then
    not drawn (+0x110 bit 0x20000, his weapons' too) until he stands.
  - The shield orc's shield (tt_vs_shield_take): half damage while it
    holds, six blows (100 or more: two) break it -- its vertices folded
    onto one point (heroes.c tt_heroes_shield_show); whole each round.
  - Swings judged where the arena gives heroes no damage (the prologue,
    Weathertop): a hero's attack box with no real hit within 450 ms.
  - Fix: a hit could count 0 -- the even-health clamp undid the health
    guard's +100000 (now only below 50000).
  - Health 1500 (from 1000), best of five (Deathmatch).
  - Nobody moves from the level's first frame until FIGHT (player 1's pad
    was only held once player 2 was in: the first seconds were free).
  - Pickups stay: a dynamic pickup's item goes at 6 s (0x8F410: +0x430 4 s,
    +0x434 6 s) and only its glow was left; their age (+0x400) is kept 0.
  - Voices of our own (versus.c tt_vs_voice, the game's level banks
    decoded): efforts on swings, cries when struck, the death cry when a
    hero falls; the game's own were seldom heard.
  - Amon Hen: the heroes stay within 50 units of the match's start (past
    it the unfinished level, a brown wall); the shield is put inside.
  - Our camera is put in again right after the game's camera update
    (0x68DC0), before the view is made.
  - The level's sfxVolume objects (type 19) are kept.
twotowers/src/heroes.c
  - Versus' three guests (N_EXTRA 10; the select, its menus, voices,
    unlocks and records keep the seven, N_SEL): the Nazgul (01WEAT1:
    AEF3DB04 / 066DCD53 / AE89108E, the Ringwraiths' sword C0F2C17C),
    the shield orc (00ISUL1 OrcMel5: 2D965DE8 / A1D1A7E9 / 3E31996A, his
    spear 075091FC, his shield 4B82EA5C in his model), Eomer (16ROHAN1
    rCaptainObj: 5654A508 / 817EFC59 / 4058E474, sword C0F2C17C). Their
    own swings, flinches, falls and deaths; the hero's kick, parry and
    throw fitted to them; voices from 01WEAT1, 00ISUL1 and 16ROHAN2; they
    throw rocks as the Uruk-hai.
  - A shield in a model whose strip has an odd count (the orc's): one
    more degenerate index keeps the winding.
  - records.dat keeps its seven guests (the Versus-only ones not kept).
twotowers/src/options.c
  - The arena row: every level but Westfold. The Versus menu's background
    is versus/menu_bg.bmp. The Mode row (Deathmatch: the smaller arenas,
    Find the Shield: the bigger); the Versus-only heroes in the hero rows.
twotowers/src/menus.c
  - Fix: Co-op or Versus pressed with the keyboard: the safety net set the
    game's next screen to 0 after the press had been handed over as
    Options (a null screen object, a hang).
twotowers/src/menus.c, options.c
  - The main menu's Versus (item 5, Exit item 6; Help moved to the bottom
    right) opens the Versus menu: each player's hero, the arena, Start.

--- 1.9.7 release fixes ---
twotowers/src/twotowers_manual.c
  - Player 2 drawn on every arena: his own place in the level's draw tree
    (0x42ED0), not player 1's (Fangorn, the Road to Helm's Deep).
  - Orthanc: our camera, starting spots on a circle; the stick goes by the
    view on our-camera arenas (the camera's turn, +0x60 / +0x84).
  - Starting spots 30 apart on a walkable line; on the bigger arenas the
    rounds after the first go on where the heroes are.
  - Kicks do almost no damage; a guard breaks after 4 blocks in a row
    (the blow then lands).
  - Each hero's own voice and the blows at full strength through the
    port's mixer; the voices at their banks' true rate (they were an
    octave low).
  - Player 2's health bar as long as player 1's, its silver strip
    mirrored.
  - Amon Hen keeps its edge (50 units); the rest of the map is for later.
twotowers/src/menus.c
  - The main menu's Co-op / Versus: when the game does not take the press
    (the menu stayed with Options under it, the next press opened
    Credits), Start is pressed for it, and if the menu still stays the
    choice is undone.
  - Versus' progress kept apart: the save data's hero records, levels and
    upgrades (0x4AB598-0x4AC740) are kept when the Versus menu opens and
    put back when Versus is left; heroes.c keeps the extra heroes'
    records and writes nothing to records.dat meanwhile. Versus' new game
    and its top level no longer reach the campaign.
xboxrecomp-0.12.0/src/video/gfx_menu.c
  - The More page without HDR, HDR paper white, Distant texture detail,
    Hero damage, Foe damage and Trigger pull needed: they stay at their
    defaults whatever the ini says.
  - The stick dead zone off the More page too (still [Input]
    stick_deadzone in the ini).
twotowers/src/options.c
  - The Gameplay Options page removed (Impossible is on the difficulty
    screens and General Options).
twotowers/src/twotowers_manual.c (high frame rates)
  - Fire and blasts: a character's or a gate's update (0x843F0, 0x90240)
    takes the fire and blast damage 0x87B80 reports every frame, while
    that damage fades with time -- at 300 fps ten times what 30 fps
    gave. The Breached Wall's gate fell to the catapults within a minute
    at 500 fps (tests: 60000 gone by 55 s; about 6000 at 30 fps). The
    damage is now taken at most once per 1/30 s of game time for each
    one hit (0x92000 from 0x90503, 0x834B0 / 0x832F0 from 0x848EE /
    0x8490D); TT_FIRE_PERFRAME: as the game did.
twotowers/src/twotowers_manual.c (credits)
  - The credits scroll by time, not by frames (too fast above 30 fps).

-- 1.9.8: co-op, the second round ----------------------------------------------
twotowers/src (co-op, with Volverine-Dude's PR #7)
  - Player 2's own animation machine (a companion of his hero's), weapons
    by slot, combo presses and attack grade per player, the combat meter
    drained once per hit, kill rewards and arrow kills for player 2, his
    own controller buttons (Co-op Options > P2 Controls), the aim dot over
    each player's lock, level triggers kept player 1's, player 2 hidden in
    cutscenes, the co-op difficulty apart from the solo game's, every
    controller in the front-end menus, the P2 Bot on the pathmaps.
  - Player 2 kept active whenever player 1 is; a late @Activate replayed
    for him; his shots leave his own hand; a beam strikes every enemy it
    touches; his health sword from his own state.
  - Explosive props (vtable 0x174980, +0x114 7) take a hit's damage
    through 0x91920 only from a role in their +0x574 mask (the player's);
    player 2's hits on them are taken as the player's
    (TT_COOP_NOP2PROPHIT).
  - A ranged killing blow the game didn't take is left at 1 with its
    damage pending, so the game's own death follows (set to 0, the enemy
    never died: no @ObjectDie, no death state). TT_COOP_PDMGKILL0.
  - An extra hero as player 2: composed on Gimli's or Legolas' set where a
    level has none of Aragorn's four (he was never built in Fangorn Forest,
    the Westfold and Legolas' levels); a box on a weapon not his goes on
    his own melee weapon (TT_COOP_NOP2BOXOWN), and is tested with his
    +0x466 up (TT_COOP_NOP2ATKFLAG).
  - A downed hero gets up from death states 52 and 54 as from 50.
  - No enemy multiplier in Fangorn Forest (its script waits on its orcs).
  - Co-op and Versus menus translated (P: keys, numbers templated).
twotowers/src/lang.c, twotowers_manual.c
  - startup.BFF's larger area for many-letter languages made in the
    contiguous window, not the game's 32 MB pool (Hindi co-op crashed at
    a level's start). TT_LANG_AREAINPOOL.
twotowers/src/options.c, menus.c
  - No input on a PC up more than 24.9 days: the Options' quiet times
    compared as signed differences and only once set.
  - The title takes the pad whose Start / A / Enter reaches it when the
    game has picked none; pad 0's handle shared between opens.
  - Back on the first Options page and a Back leaf on our pages.
xboxrecomp-0.12.0/src/kernel/xbox_memory_layout.c, kernel_bridge.c
  - Freed contiguous blocks reused (DirectSound's stream packets were
    never given back: sound went silent some forty minutes in).
xboxrecomp-0.12.0/src/kernel/nv2a_sw3d.inc
  - A texture slot's "looks like a movie" count reset after 1.5 s without
    a decode (Ultra text dropped off menus after reloads).

-- 1.9.9: The128ReBits' R8 -------------------------------------------------------
twotowers/src (The128ReBits' R8, PR #11, merged on 1.9.8)
  - Six new extra heroes from the disc's NPC assets (elvenwarrior,
    elvenarcher, troll, rohan, urukcap, gandalfwhite; skins 7-12, the
    Versus-only three after them), the Xbox animation codec fix, native
    combos and weapon profiles, the playable troll, per-character records
    by name (TTH3), atomic unlocks, extra_heroes modes 0-3.
  - Five language axes (text, subtitles, in-game voices, cutscene voices,
    films); SRT film subtitles drawn over Bink (Windows); non-USA XBEs
    refused; the optional DLSS-5 hook; tools/regions.
xboxrecomp-0.12.0/src/kernel/kernel_path.c
  - Partition1's UDATA and TDATA under save_dir (<game>/saves), with the
    old ones copied there once (Win32, and POSIX in 1.9.9).
On top of the merge
  - lang.c / lang_fmv.inc / heroes: build on Linux and macOS (directory
    listing, rename(), the SRT overlay Windows-only for now).
  - The six as co-op player 2 (their projectiles: troll and Uruk captain
    a rock, the elves Legolas' arrow, Gandalf the White Gandalf's) and in
    the Versus hero list; their short names in English.
  - Player 2 as a retail hero beside a different player 1 hero: his box
    tested with +0x466 up, as the extra heroes' since 1.9.8 (Deeping Wall:
    69 boxes, 17 with the flag up, 6 contacts; after: all, 71 contacts).
    TT_COOP_P2ATKFLAGGUEST.
twotowers/tools/appimage
  - The Linux AppImage: AppRun (the game's folder beside the AppImage),
    pysquashfs.py (a SquashFS 4.0 writer in Python), make_appimage.sh.

-- 1.9.9: the roster, Troll Battle, player 2 ---------------------------------------
twotowers/src/heroes.c, heroes_ampliacion_textos.inc, heroes_troll_throw.h
  - Gandalf the White replaced by Saruman (Orthanc's: mesh 8F3B37CA, set
    56D8D87C, texture EF4942FD, his staff 30028EA4), his own map of the
    hero's slots, his own upgrade names and green-yellow / yellow beams
    (effect modes 3 and 4, by turns).
  - The Rohan captain and the elven archer removed; the elven warrior,
    the troll, the Uruk captain and Saruman are Versus heroes (the menus
    keep the seven originals, N_SEL 7).
  - The troll's ranged attack: Fangorn Forest's troll's throw (14FANG2
    set 0x0215F674, slot 66) with its bones put in the cave troll's order,
    in place of the bow. TT_TROLLBOWTHROW. His voices are Balin's Tomb's
    (1406-1414).
twotowers/src/options.c
  - Versus "Mode:" row: Deathmatch, or Troll Battle (both players the
    cave troll; Balin's Tomb, Amon Hen or Mount Doom).
twotowers/src/twotowers_manual.c
  - Versus: the players start further apart (42); a player off a ledge
    loses the round (TT_VS_NOFALLLOSS); the kill move (RT) does no damage
    the first two times in a round ("STAB GUARD: n LEFT") and then at most
    40% of full health (TT_VS_STABPCT); a parried blow rings with the
    campaign's clang (0xDD), a landed one with the flesh hit the game last
    played; the shield orc's shield rings, his grunt under it, a crack and
    his cry when it breaks (TT_VS_NOSHIELDSND).
  - Player 2's rumble: the game's per-port rumble run (0x1F8A0) made for
    port 1 too, and his virtual pad's motors sent to the PC's second
    controller (TT_COOP_NOP2RUMBLE).
twotowers/src/versus.c
  - A random level's music for each match (TT_VS_FIXEDMUSIC); the heroes'
    blows at full strength and their steps louder; a hero's sound not in
    the arena's bank taken from his own level's; the troll's swings his
    club's slam (TT_VS_TROLLWHOOSH).
twotowers/src/e3hud.c
  - Combo runes and labels per player: player 2's (port 1) on his side,
    mirrored; alone, any controller is player 1's.

-- 1.9.9: walking, the troll's log, Saruman in the campaign ------------------------
twotowers/src/twotowers_manual.c
  - Co-op and Versus: the walk timer (0x43B0FC: the control step raises
    INP_WALK once a part push has been held a moment) is per player now;
    the other player's step put it back to 0 each frame, so neither could
    walk and an idle hero stood in the walk's first steps over and over.
  - Versus: a hero's lock on the other only while he fights (a press in
    the last 1.5 s, TT_VS_TGTMS) and within 7 units (TT_VS_TGTR): held from
    anywhere it kept him in the engage stance's loop at rest and steered
    his moves round the other (TT_VS_OLDTGT: as before).
  - The heroes take more hits: full health 125% in the campaign and co-op
    (TT_HPPCT), Versus 2000 (was 1500).
  - The cave troll's club ends each swing with Balin's Tomb's slam (930)
    in every mode (TT_TROLLSLAM_MS, TT_NOTROLLSLAM).
twotowers/src/heroes.c
  - Saruman in the character select after Sauron (one win more), his
    name picture (tools/names), the campaign and co-op.
  - The troll throws Fangorn's trolls' log (projectile 8; its prop wears
    the bark texture, added from Fangorn Forest, in his levels).
    TT_TROLLROCK: the rock.
  - Troll Battle: player 2 is the other troll, Fangorn's lighter one, on
    the cave troll's moves (his bones put in the cave troll's order).
  - Troll back at skin 9 (heroes_weapon_profile.h and the build's troll
    patches name it there).
twotowers/src/menus.c
  - (test) TT_P1SCRIPT steps "LY=<value>": a part push of the stick.

-- 1.9.9: player 2's moves, Saruman's own moves, input, languages as in 1.9.8 ------
twotowers/src/heroes.c
  - s_pools back in s_skins' order (Saruman, Uruk captain, troll at 7, 8,
    9): Saruman had the troll's moves (his casts stood still, his ranged
    was a bow pull) and the troll Saruman's. Saruman's own moves again,
    his float for the ranged; a check at start names a pool out of place.
  - Saruman's select: Orthanc's laugh (21ORTH2 2621) and cast.
  - The troll's and Saruman's ranged pictures (hudammo, upgitems): the
    tree and the staff, with alpha.
  - Player 2's launches are his: player 1's guest beam no longer replaces
    Legolas' arrows as player 2.
  - Player 2's special move names on his side, with his own names.
twotowers/src/twotowers_manual.c
  - The special move name (HUD element 1.15, text 0x4A4A98) raised in
    player 2's turn is shown mirrored on his side; player 1's left alone.
  - The troll's slam armed before Versus' sound routing (it never played
    there).
xboxrecomp-0.12.0/src/input/xinput_device.c
  - On Windows itself the keyboard is read from the start: the Enter that
    launched the game stayed held as Start, so the controller's Start
    never paused and the main menu kept choosing (only a left Alt let it
    go). A key the keyboard reports up is let go after 250 ms.
twotowers/src/lang.c, setup_launcher.cpp, main.c, menus.c
  - Languages as in 1.9.8: one language for the game and the launcher.
    The R8 subtitle/voice/film language choices, the SRT subtitles, the
    launcher's interface-language and DLSS 5 buttons, the DLSS 5 hooks and
    the USA-XBE-only check taken out (dlss5.inc, lang_fmv.inc,
    xbe_version.inc removed).

-- 1.9.9: Versus falls, player 2's move names, the Berserker, Lurtz and the captain on the ground --
twotowers/src/twotowers_manual.c
  - Versus: none of the co-op's rules that put player 2 beside player 1
    (start of play, catch-up, a fall below him, player 1's jump, the
    pathmap edge, the leash). They undid the arena's spacing and wrote
    player 1's height at a spot on other ground: under a rise there, a
    hero fell through the map and the screen went black (Fangorn).
  - A hero set on a new spot (the spread at the start, going on in the
    bigger arenas) is put TT_VS_LIFT (8) units over the higher of the two
    and drops onto the ground there.
  - The edge guard: the spot he is put back on counts as safe only once
    he stands there; put back and fallen again at once, beside the other.
  - Only a kick off a ledge ends a round (within TT_VS_KICKFALLMS, 4 s,
    of the other's kick); a hero who falls on his own is put back.
  - Player 2's special move name: 0xCCB30 hooked -- raised in his
    context it is his, shown on his side; a retail hero's own name for
    the move (the game's table), a guest's from heroes.c. The old check
    waited for the shown flag, which comes a tick later, and missed all.
  - (test) TT_VS_TESTROUND, TT_COOP_P2NAMETEST.
twotowers/src/heroes.c
  - Saruman is Versus-only again (the carousel back to seven).
  - The Uruk-hai is the Berserker: his name, his name picture
    (tools/names), Versus' list and his upgrade names.
  - Retargeted falls and hits keep his lowest point at half the hero's
    height over the floor (rt_ground_fix, TT_RT_GROUNDK, TT_RT_NOGROUND):
    Lurtz lay flat in the air (lowest bone 3.7 over the floor, now 1.6).
  - Lurtz's light attacks 45 and 74 (his 41/42 are a leap, 1.8 off the
    ground); the captain's heavies 172 and 69 (43 left the ground), his
    own falls (the Uruk-hai's) instead of the hero's.
  - (test) TT_ZSCAN and TT_SLOTSCAN print how far each move leaves the
    ground.

-- 1.9.9: Troll Battle's health, results and records; Versus music; own first swings --
twotowers/src/twotowers_manual.c
  - Troll Battle: 4000 health (TT_VS_TROLLHP), twice Deathmatch's.
  - Troll Battle's own results: the trolls' numbers and its records
    (biggest smash, fastest knockout, most damage in a match; kept in
    troll_records.txt beside the game, new ones marked NEW!).
  - Sounds per mode: versus\round_start.wav at each round's banner (new);
    in Troll Battle versus\troll_<name>.wav first (fight, round_start,
    round_win, match_win), else the usual. Its results picture
    versus\troll_results_bg.bmp first, else results_bg.bmp.
  - Versus music started once a level load, not once a level index: the
    second match on an arena (Amon Hen twice running) had none.
  - (test) TT_VS_TESTROUNDP: whose fall TT_VS_TESTROUND makes.
twotowers/src/versus.c
  - tt_vs_play_mode_wav, the Troll Battle records (tt_troll_record).
twotowers/src/heroes.c
  - Slot 35, the first swing from the stance, is a light attack: the
    guests swung Aragorn's (retargeted) before their own -- Lurtz, the
    trolls. Lurtz without the hero's thrust ('t'): his own swings only.
  - The ground fix at 0.3 of the hero's height (Lurtz lay 1.3 up).
xboxrecomp-0.12.0/src/apu/apu_vp.c
  - (test) RECOMP_APU_STREAMTEST (the streams' share), RECOMP_APU_STREAMLOG.

-- 1.9.9: Lurtz on the ground --------------------------------------------------------
twotowers/src/heroes.c
  - rt_ground_fix measures as the game draws: the root bone's (0) moves
    left out (the game moves the actor by them; their height is never
    drawn), the height put on the root's child (bone 1). The hero's lowest
    point kept over the floor in proportion (TT_RT_GROUNDK, 1): knocked
    down, Lurtz lay 3.8 up, now 2.3 as Aragorn's 1.8 (the poses' lowest
    bones; in game both on the ground).
  - Lurtz's hit 21 (a stagger 3.8 back and a unit up) is his flinch 20.
  - (test) TT_ZSCAN's noroot lines.
twotowers/src/twotowers_manual.c
  - (test) TT_VS_DOWNSHOT / TT_VS_DOWNSHOTMS: frames while a hero is down;
    TT_COOP_DOWNDUMP.

-- 1.9.9: Versus camera by the edges, the shield orc's shield breaks, names -----------
twotowers/src/twotowers_manual.c
  - Versus camera: the eye counts as inside the level over any of its
    pathmaps (only the hero's own did); outside them all it turns round
    the heroes (2 degrees a frame) toward a heading whose eye is inside,
    and is drawn in to 70% at most (was down to 2 units, and it stayed:
    the Hornburg Courtyard, the Plains of Rohan). The Courtyard on ours
    from the start. (test) TT_VS_CAMLOG.
  - (test) TT_VS_SHIELDBREAKTEST.
twotowers/src/heroes.c
  - The shield orc's shield on his own skin (12; the table still said 14,
    from before the roster changed): it is built into his model again,
    found in memory and folded away when it breaks.
  - The shield orc's combo names, and the trolls' (both trolls): names
    only, his moves are not upgraded.

-- 1.9.9: player 2's bow, the whole map in Versus, hits from any side, the camera ------
twotowers/src/heroes.c
  - skin_p2_ranged / tt_heroes_p2_ranged: the ranged weapon a character
    shows as player 2, as he does as player 1 -- the retail heroes their
    own, Boromir and the other guests with a bow Aragorn's (what they draw
    in the campaign), none for those who shoot from an empty hand or a
    staff (Frodo, Gandalf, Sauron, Saruman, the trolls...) or hold their
    own bow already (Lurtz). Its model and texture are brought into the
    level with player 2's other resources.
twotowers/src/twotowers_manual.c
  - Player 2's ranged weapon for every character (it was Legolas, Gimli,
    Aragorn and Isildur only): copies of the level's ranged weapons in his
    model, or, with none in the level (player 1 Frodo or Gandalf, Versus),
    a copy of one of player 1's weapons in it. His own copies are no longer
    copied again ("no loader key").
  - Player 1 a guest who shoots from an empty hand: his moves, which player
    2 plays, took player 1's empty bow and his blade for an arrow, and
    player 2 Legolas or Boromir aimed with nothing in his hand. His bow
    and arrow are taken in their place; when the aim is over the bow goes
    onto his back and his other weapon into his hand. TT_COOP_NOP2BOWSUB.
  - Versus: every sector of the level drawn (the flag at 0x3C35C8 and an
    all-shown array at 0x3C35CC, as a level without portals): the parts the
    campaign's scripts open were never drawn, the ground ended in a brown
    void (Amon Hen). TT_VS_KEEPSECTORS.
  - Versus: the level's props (type 7: the campaign's barriers, a trunk
    across Amon Hen's path) out of the collision world as
    @ObjectCollisionOff does (0xC67B0 on 0x43C5E8). TT_VS_KEEPPROPS.
    Amon Hen's 50-unit arena edge is gone.
  - Player 2's ammo icon and the Versus results picture: their texture
    object in the same block as the pixels -- alone it could come back at
    0x80000000 (physical 0) and the icon was drawn black (player 1 Isildur).
  - Versus hits: a swing lands on a hero within its reach on any side --
    the game's own facing test (a hit only in front) is no longer the
    judge (TT_VS_OLDJUDGE). Reach by size: 3.6, the Berserker, the captain,
    Lurtz and Sauron 4.2, the trolls 5.5 (TT_VS_REACH).
  - Versus camera: no longer drawn in at the map's edges, it stays out
    (the Plains of Rohan; TT_VS_CAMSHRINK: as before).
  - (test) TT_VS_POSLOG, TT_VS_GLIDE, TT_VS_SHOTS (TT_VS_SHOTSMS, _SHOTSN),
    TT_VS_WALKTO (player 1 walked along a route over the pathmaps),
    TT_VS_CAMP1, TT_OBJNEAR / TT_OBJDUMP, TT_WALKGRID, TT_COOP_TAKELOGN.
twotowers/src/menus.c
  - (test) TT_TPANY, TT_P1POSLOG, player 1's stick for TT_VS_WALKTO.

-- 1.9.9: Amon Hen walkable from start to finish in Versus -----------------------
twotowers/src/twotowers_manual.c
  - Versus: the seams between pathmaps opened. A hero walks only on his
    pathmap's open cells (state 3 is a wall) and every pathmap is walled at
    its own edge; the campaign moves its heroes on from one to the next with
    its story. Where two pathmaps meet a hero stood at an invisible wall
    (Amon Hen past the start's forest, by the river). A cell walled in one
    pathmap and open in another over the same ground is opened, once a level
    (874 cells in Amon Hen). TT_VS_KEEPSEAMS. A walk test now goes from the
    start through the forest, the ravine's far side and the river path to
    the level's end.
  - Versus gates: Lurtz's ruins (Amon Hen's pathmap 14) are walled in all
    round and the campaign takes its heroes there with the level's own
    teleport (playerTeleportObj, its record's +0x28). Walking into the end of
    the river path (564 -377) takes both heroes into the ruins; the ruins'
    east wall (282 -398) takes them back. TT_VS_NOGATES.
  - Versus falls: a hero is off the map only below the level's lowest ground
    there (the lowest of the pathmaps over him, 12 under; vs_floor) -- a long
    drop onto lower ground no longer puts him back. A kick down off a ledge
    still wins the round.
  - The step through walls held against (vs_unstick) is off by default
    (TT_VS_UNSTICK): past them lay the voids between the level's pieces.
  - (test) TT_VS_TOPCAM (frames from straight above, tile by tile),
    TT_RECDUMP (placement records by name), TT_WALKCAMPAIGN, TT_SETPOSLOG,
    TT_WORLDDUMP; the walk test (TT_VS_WALKTO) steps round obstacles and
    reroutes after a fall.
  - Fix: entering Amon Hen in Versus crashed on Windows. The seams' walk took
    every pointer in the level's pathmap list, and past the level's own the
    list can hold leftovers -- one with a size of millions sent the walk out
    of memory (Linux reads that memory without a fault). vs_pm_ok checks a
    pathmap as the camera's list does, for the seams, vs_floor and the walk
    tests; the neighbour's cell is bounds-checked before it is read.

-- 1.9.9: random starts, the Ent, breakables back, walls by every pathmap ------------
twotowers/src/twotowers_manual.c
  - The level's props keep their collision again (they are its breakables:
    with it off no blow broke the logs and barrels). TT_VS_PROPSOFF: off.
  - Every sector drawn at once only on Amon Hen (TT_VS_ALLSECTORS: all):
    on Westfold it lost the ground and player 1 (the Plains of Rohan
    playtest's missing player 1).
  - Pathmap walls: no longer all opened. Opened where another pathmap goes
    on (the seams, as before) and in thin walls -- 1 to 3 cells with open
    ground on both sides (TT_VS_KEEPTHIN). The map's edge stays, now for
    both heroes: a step onto ground no pathmap has open is undone
    (vs_union_walk; player 2 was held by none and walked off the map),
    except for a hero just knocked flying (a kick off a ledge still wins).
  - Random starts: each match begins at one of the level's own points on
    open ground (its waypoints' and spawners' placement records), player 2
    beside player 1; every later round goes on where they stand, on every
    arena (TT_VS_FIXEDSTART, TT_VS_RESETSPOTS: as before). A hero set
    somewhere takes that spot's pathmap (tt_vs_set_pm).
  - The Ent hits as far as the trolls (5.5).
twotowers/src/heroes.c
  - The Ent (Treebeard, Fangorn Forest's TreebeardObj: mesh 4B2413DE, set
    F0B6229A, texture 7DED492E, 41 bones mapped onto Aragorn's by
    tools/rig/chain.py): his own stance and walk (his set has four motions),
    the hero's swings, parry, kick, throw and falls fitted to him, bare
    hands (no props drawn); as player 2 his throw is the troll's log. N_EXTRA 16.
twotowers/src/options.c
  - The Ent in the Versus list, and beside the cave troll in Troll Battle.

-- 1.9.9: enemies in Versus, Troll Battle's own end, Perfect glow for player 2 --------
twotowers/src/twotowers_manual.c
  - Versus with enemies: on Amon Hen, Fangorn, the Plains of Rohan and the
    Breached Wall (Deathmatch, not Troll Battle) a few of the level's own
    orcs and Uruk-hai are made (vs_keep_enemy: by chance, at most
    TT_VS_NENEMY 8; no archers, no named leaders), then spread over the map
    in pairs at the level's points, away from the heroes. They fight both
    heroes with their own sounds and die as in the campaign. Their blows
    are told from the other hero's (an enemy's attack box within 3 s and 12
    units, the other not swinging close by) and do a player's damage
    (TT_VS_FOEDMG, 60); they count for neither player's numbers, and an
    enemy's knock back off a ledge gives the other no round. The heroes
    lock onto an enemy nearer than the other hero while they fight.
    TT_VS_NOENEMIES: off.
  - Fix: the placement record table was never emptied. After a few matches
    it was full: a new level's records were not noted -- player 2 had none
    of his own weapons and the random starts found no points (the Plains of
    Rohan playtest) -- and old levels' records gave starts in the void.
    Each level's records are now apart (coop_reckey_lv0); a full table
    drops the earlier levels'.
  - Random starts: only points on a pathmap's ground (not Fangorn's
    spawners up in the trees) and inside the level's sectors. Every sector
    is drawn on Fangorn and the Gates of Moria as on Amon Hen: a start past
    the first sectors showed only the sky.
  - The round's loser, if he was down, goes on at one of the level's own
    points about 42 units from the winner (tt_vs_spot_from); set apart along
    a line he could land on a ledge with no way back (Balin's Tomb).
    Player 2 steps through a wall he is held against (vs_unstick, now on for
    him; player 1's with TT_VS_UNSTICK), back if there is no ground there.
  - Co-op: player 2's Perfect glow -- the gauge's glow (list 1, 7-9),
    mirrored onto his gauge and pulsing while his Perfect time runs; the
    game showed it only on player 1's. TT_COOP_NOP2GLOW: off.
  - Co-op: player 2's weapons drawn again after he respawns (the +0x110 draw
    bits recorded when he falls, put back for 4 s).
  - The Ent's blows end in the troll's ground slam; a blow's reach counts
    the other's bulk (2.5 for the trolls and the Ent): blows at his side or
    back land.
  - Troll Battle's results: its own table -- club blows of swings, smashes
    (300 or more), damage dealt and taken, throws, fastest knockout.
  - (test) TT_VS_FOENEAR, TT_VS_FOELOG, TT_VS_SPOTLOG, TT_VS_SPOTPICK,
    TT_VS_HITSCAN, TT_COOP_P2KILLMS, TT_PERFDIFF, TT_GLOWPROBE.
twotowers/src/heroes.c
  - The Ent throws rocks (projectile 10), as player 1 and as player 2; his
    own special move names.
twotowers/src/options.c
  - Troll Battle is the cave troll's only; the Ent is a Deathmatch hero.
    Both players may pick the same hero.
twotowers/src/versus.c
  - The Ent's voice: Fangorn's Ent moans (S_WB_14FANG2.xwb, Ent_01-03) for
    his blows taken and his fall.
versus\ (the player's own files)
  - troll_results_bg.bmp, troll_fight.wav, troll_match_win.wav for Troll
    Battle; round_win.wav, the horn, at each round's end.

-- 1.9.9: the Ent's blows land, Hornburg's starts and enemies, Balin's Tomb kept to its floor --
twotowers/src/twotowers_manual.c
  - The Ent's blows: his hands hold props of no triangles that the game
    never places -- they stayed at the level's origin, and his attack
    boxes' tests ran there: no orc hit, nothing broken. They are placed at
    his reach each frame, swept side to side across the front of him and
    turned as he is (tt_vs_ent_hands; the box's own weapon noted at 0xBA020).
    Player 1 or 2, he now hits and fells the enemies. TT_VS_NOENTHANDS: off.
  - A hero's attack box keeps its own 0x01000000 bit beside the Versus mask.
  - Random starts on the Hornburg Courtyard: it has no waypoints or
    spawners -- its BattlePoints and Enemy_Waypoints are taken (it always
    began at the level's start). A sector test that finds no sector with a
    box lets every point through; "Default" counts (Balin's Tomb's hall).
  - Enemies on the Hornburg Courtyard too.
  - An arena with enemies keeps its own sound banks and plays its own music:
    with another level's banks in their place the orcs were silent.
  - Balin's Tomb: starts on the hall's floor only (no ledge or catwalk), the
    heroes 16 units apart (vs_spacing); player 2 at the match's start, and
    a downed hero after a round, at one of the level's own points.

-- 1.9.9: Versus achievements, notes that say why, the PS3 pad on Linux, the camera round big heroes --
twotowers/src/achieve.c
  - 25 more achievements for Versus: Troll Battle (1, 3 and 10 matches won,
    a match without a round lost, a 400 smash, 25,000 damage in all, a round
    in under 20 s, 25 rocks or logs landed), the levels' enemies (25 and 150
    slain, a Deathmatch won on all five enemy arenas), 3 matches won as each
    of Saruman, the Ent, the Uruk Captain, the Elven Warrior, Lurtz, Sauron,
    Gandalf, Elrond, Boromir, Frodo and the Berserker, the Ent's 20 rocks,
    and a mirror match of first to 5. Counted across sessions in
    achievements.txt (count lines; N_CNT 64).
  - An earned achievement's note shows its description under its name.
xboxrecomp-0.12.0/src/video/gfx_menu.c
  - xbox_ToastShow3: a note with a third, wrapped line (up to 3 lines), 7 s.
xboxrecomp-0.12.0/src/input/xinput_device.c
  - Linux: a DualShock 3 is read through the kernel's driver (hid-sony,
    evdev), SDL's default there; forced onto SDL's HIDAPI driver it was found
    and named but sent nothing when its hidraw node was readable (GitHub
    issue 14). TT_PS3_HIDAPI=1: the HIDAPI driver all the same.
twotowers/src/twotowers_manual.c
  - Versus camera: with the Ent or a troll in the match, the eye 9 units
    further back and 6 higher -- the Ent's trunk filled the screen between
    it and the other hero (TT_VS_BIGCAM=0: off). The camera's own distance
    (+0/+4) held at its most this level (TT_VS_CAMKEEPDIST).
  - Enemies slain in Versus counted for the nearer hero; Troll Battle's and
    the heroes' match wins, round times and hits reported to the
    achievements.
  - (test) TT_ACH_TEST, TT_TOASTDUMP, TT_VS_CAMDUMP, TT_GLOWPROBE.
twotowers/src/twotowers_manual.c
  - Co-op: player 2's arrows and throws hit the Watcher. The projectile step
    (0xDAD20) tests the fight's list of targets (0x43B0E8,
    @ForcePlayerWeaponObbTestOn) only for projectiles owned by [0x43B15C],
    player 1; one of player 2's is stepped in his context, so the test runs
    for it too (TT_COOP_NOP2PROJLIST: as before).
  - Co-op: player 2 held where he stands (the Gates of Moria's lake: wading,
    held by the water's unseen wall) -- the stick pushed, under 0.4 units
    moved in 2 s and over 6 units from player 1 -- is put beside player 1,
    whatever the P2 Teleport setting (TT_COOP_NOSTUCKPUT: off).
  - Versus, Mount Doom: the whole arena is playable, over the lava and on its
    far side. The pathmap's walls inside the map are opened; a hero held at
    the lava's edge while pushing on is carried over it at a walk, level
    with the bank (his collision step skipped meanwhile, sub_000B9530); a
    hero dropping into a pit is held at the height he stood at and can walk
    on or out; the game takes him back once the ground holds him
    (TT_VS_DOOMKEEP: as before; TT_VS_DOOMSPD, TT_VS_DOOMLOG). Spawn points
    on the far side are used; those right at the lava's edge are not
    (TT_VS_DOOMANY: all points).
  - (test) TT_VS_PMDUMP: each pathmap's cells as a PGM.
twotowers/src/twotowers_manual.c, versus.c, menus.c
  - Versus, Mount Doom: player 2 is carried over the lava too. He never goes
    through sub_000B9530: his own step (0x843F0) and the game's SetPosition
    on him (from 0xBD609) are undone in tt_coop_leave while he is carried,
    and he is set where the carry put him; vs_unstick is off there (it put
    him back where he began pushing); the open ground ahead is read from
    the pathmap's own cells (the overlay at 0x418C00 marks the cells round a
    character, a troll's included).
  - Versus music: Fangorn's and Weathertop's tracks are no longer drawn for
    other arenas -- long quiet stretches (Fangorn's 14F2ALT1 about 20 s at a
    time) left Mount Doom without music. (test) TT_VS_MUSIDX, TT_VS_MUSDUMP.
  - Versus: player 1's controller port is held for the match (taken at the
    first FIGHT, put back if it changes in his context): in a Troll Battle
    from round 2 on player 1 could not steer, moving with player 2's stick
    (TT_VS_NOPORTHOLD: off). (test) TT_VS_P1MOVELOG.
  - New Game's difficulty screen: Easy, Normal and Expert answer the mouse
    (pointed at: the cursor; clicked: chosen), not only Impossible.
xboxrecomp-0.12.0/src/input/xinput_device.c
  - The movement keys held unchanged for over 4 s are logged every 5 s
    ([INPUT] movement keys held ...), each with where it is seen held (the
    keyboard itself, or only the window's record) and which window is in
    front -- for a playtest's player 1 who moved one way whatever he pressed.
xboxrecomp-0.12.0/src/video/fb_present.c
  - Every key (not the mouse buttons) let go as the window is activated,
    deactivated or gets the keyboard back, besides losing it (WM_KILLFOCUS).
twotowers/src/options.c
  - Troll Battle's arenas: Balin's Tomb and Amon Hen (Mount Doom taken out).
twotowers/src/twotowers_manual.c, xboxrecomp-0.12.0/src/input/xinput_device.c
  - Versus: player 1 pushing the stick and not moving for 2 s is logged
    ([VS] player 1 held at ...: his state, his stick as the game reads it,
    the last caller of SetPosition on him) -- for the Troll Battle's player
    1 held on one spot. (test) TT_VS_PMNEAR.
  - The held-movement-keys log only after 4 s (its start time had made it
    log at once).
twotowers/src/twotowers_manual.c
  - Co-op: player 2 held where he stands is put beside player 1 only with
    the Co-op menu's P2 Teleport on (with it off, no teleport at all).
twotowers/src/main.c
  - A crash report gives the program's start and the crash's offset from it,
    and the native return addresses as offsets (the program loads somewhere
    else each run; the old check looked for the fixed 0x140000000 only).
twotowers/src/twotowers_manual.c, menus.c
  - The level's pathmap list read only through addresses the game's memory
    has (coop_va_ok: its 64 MB, or the contiguous window at 0x80000000):
    past its own entries the list holds leftovers, and one at 0x76EA0xxx --
    let through by the old "under 0x84000000" check -- crashed the game in
    a Troll Battle on Balin's Tomb (vs_floor, a playtest's crash log).
twotowers/src/twotowers_manual.c, menus.c
  - Versus, the Plains of Rohan: starts only on the start's open field (its
    Sector_01, south of the village's overlapping Sector_02), with 8 units
    of open ground round them (vs_clearance), and points of our own every 8
    units there besides the level's few; the round's loser set by the winner
    as before. A hero set down in the village's sectors was not drawn (the
    level draws its characters by the sector they came into), or stood
    under a roof or behind a wall. TT_VS_ROHANANY, TT_VS_CLEAR.
  - Versus bot: after each blow that lands, 1.8 s (2.6 s in a Troll Battle)
    before it swings again, stepping back meanwhile -- swinging in its beats
    it kept player 1 stunned on one spot (TT_VS_BOTNOREST: as before).
  - Versus camera on the Hornburg Courtyard: the eye must see the heroes --
    no walled cell between it on the pathmaps (vs_eye_sees); its pathmaps
    reach past the walls, and an eye behind the south wall was drawn in by
    the game and stayed close (TT_VS_CAMLOS: 1 every arena, 0 off).
  - Versus pause menu: Restart Match -- a note at the top while paused;
    BACK (Backspace / Tab) or a click on it starts the match again from
    round 1 (scores 0, both whole at the match's starting spots) and closes
    the pause. TT_VS_NORESTART: off.
twotowers/src/twotowers_manual.c
  - Versus camera: the camera's second look-at and eye (+0x64..+0x6C,
    +0x74..+0x7C, the game's own after its wall test) set to ours each frame
    -- on the Hornburg Courtyard the game drew its eye in to a wall and the
    view stayed close while ours was far (TT_VS_CAMNOCOPY: as before).
  - Restart Match: a new random start, as a new match's.
  - Versus pause menu: Restart Round -- a second line under Restart Match;
    Y (the I key) or a click on it plays the round again (the score kept; a
    round already won stays won, the next one starts).
twotowers/src/heroes.c
  - Hero names longer than "Isildur" in full: the Berserker read "Berserk"
    on the character select and the level map. The string table's entry is
    pointed at a longer slot (the room of a string the table holds twice,
    whose twin then reads the first copy). TT_NAMESHORT: as before.
twotowers/src/twotowers_manual.c
  - Versus, falling off the map: a hero put back and falling again is put
    beside the other only while the other stands on ground; otherwise (or a
    third fall in a row) where he began the round. On the Hornburg
    Courtyard's south-east edge both heroes went off, each was put beside
    the other below the floor, and the two sank through the world with the
    camera following -- the view "zoomed in" to the flagstones and stayed
    (TT_VS_OLDBACK: as before).
  - Versus camera: a falling hero counts at the height of his last ground,
    so the view no longer sinks under the floor with him (TT_VS_CAMFALL: as
    before).
  - Versus, Hornburg Courtyard zoom (the real cause): crossing the level's
    camera plane trigPlane_camera_Courtyard1 on the west side handed the
    camera the level's own close view (a 13-degree lens), and nothing handed
    it back -- the picture stayed zoomed in through restarts until the game
    was left. The level's camera planes are left alone there now (the camera
    is ours) (TT_VS_CAMPLANES: as before).
  - Versus, Hornburg Courtyard: the yard's one-cell outer walls are no
    longer opened as "thin walls" -- the heroes walked out through the west
    wall into the stone and fell (TT_VS_THIN11: as before).
  - Versus camera on the Hornburg Courtyard: its line-of-sight test counts
    only the ground at the heroes' height, so the eye no longer sits inside
    the ramparts that overhang the yard (TT_VS_CAMLOSFLAT: as before).
  - Versus with enemies: an enemy's blow before the round begins (the
    countdown, between rounds) hits no one (TT_VS_FOEPREROUND: as before).
  - Co-op: no leash while player 1 is down -- player 2 goes anywhere until
    he is back (TT_COOP_LEASHDOWN: as before).
  - Versus pause menu: Restart Round picks a new random start too, as
    Restart Match does (the score kept).
  - Co-op: player 2 takes any pickup he walks over when he needs it --
    the level's own (the arrows by Balin's Tomb's cave troll) came only
    for player 1 (TT_COOP_NOP2PLACED: as before).
  - Co-op: the leash 28 units (was 20): a little more room for player 2.
  - Versus: the round's pickups are put out at "FIGHT!", where the players
    stand -- put out as the round began they hung over the ground (the
    players are dropped in from above), out of everyone's reach.
  - Co-op: no leash -- player 2 roams as he does while player 1 is down
    (TT_COOP_LEASH=units: one again). Far apart (past 30 units) the camera
    goes over to player 1; P2 Teleport (on) still brings player 2 back
    past 42 units.
  - Co-op pause menu: a note at the top turns P2 Teleport on or off -- Y
    (the I key) or a click on it -- without going to the settings
    (TT_COOP_NOTELEKEY: none).
  - Co-op: player 2's hits damage every object player 1's do -- boxes,
    Fangorn's log, the Breached Wall's catapult, the enemies' shields (0x91920
    takes his role's hit wherever it takes the player's, not only on the
    props) (TT_COOP_P2PROPONLY: the props only, as in 1.9.8).
  - Co-op HUD: a boss bar of another shape (the Breached Wall's catapult)
    is moved under player 2's bar whole -- half of it stayed on his bar. A
    top-right list's top pieces move together (TT_COOP_ALLYRIGHTONLY: as
    before).
  - Versus, Orthanc's summit: a hero can no longer walk or be knocked past
    the parapet (33 units from the tower's middle; TT_VS_ORTHMAX) -- he
    slides along it. One that still ends up off the top (below the floor or
    past the parapet) is put back on the summit at once, so a new round
    never starts with a player falling forever (a kick off still wins the
    round). The pathmap a hero is placed on is picked by his height too
    (Orthanc's five lie one over another). If the level's own scene keeps
    the HUD hidden, the match begins anyway after 10 s instead of leaving
    both players unable to move (TT_VS_WAITSCENE: wait for it).
    TT_VS_ORTHNOWALL: none of this.
