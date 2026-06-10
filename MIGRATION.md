# PicoDVI: Audio/HDMI Migration — rp2240 → rp2350

## Overview

Both `PicoDVI_rp2240` and `picoDVI_rp2350` share the same base commit
`51237271437e9d1eb62c97e40171fbf6ffe01ac6` ("More snowflakes").

- **`PicoDVI_rp2240`** — Original PicoDVI fork for RP2040, extended with HDMI
  data island and stereo audio output support.
- **`picoDVI_rp2350`** — Port to RP2350 with extended GPIO support. Audio/HDMI
  support has been ported from rp2240 with RP2350-specific fixes and
  improvements.

This document describes all changes made when porting audio/HDMI support from
`PicoDVI_rp2240` to `picoDVI_rp2350`, and the fixes applied in this migration.

---

## What Was Added (original rp2240 audio implementation)

### New library files in `software/libdvi/`

| File | Purpose |
|------|---------|
| `data_packet.h/.c` | HDMI data island packet structure, TERC4 encoding, aux data units |
| `audio_ring.h/.c` | Lock-free ring buffer for audio samples (producer/consumer across cores) |
| `audio_sample.h/.c` | Audio sample packet framing (IEC 60958 / HDMI channel status bits) |
| `data_island_encode.h/.c` | TMDS encoding for the data island period (TERC4) |
| `dvi_audio.h` | Builders for AVI InfoFrame, Audio InfoFrame, Audio Clock Regeneration packets |

### DMA list restructuring (`dvi_timing.c`)

The DMA control block lists for each TMDS lane were expanded to carry HDMI
data island packets in the blanking intervals:

- **`dvi_setup_scanline_for_active()`** — Added `bool black` parameter to
  select between black scanline TMDS symbols (guardband) vs. standard empty
  symbols.
- **`dvi_setup_scanline_for_vblank_with_audio()`** — New function. Inserts a
  data island (preamble + data packet + back-porch) into the vertical blanking
  interval. Sync lane: 5 control blocks; non-sync lanes: 5 control blocks.
- **`dvi_setup_scanline_for_active_with_audio()`** — New function. Adds a
  video guardband before active pixels and a data island in the front porch.
  Sync lane: 6 control blocks (active TMDS at `[5]`); non-sync: 7 blocks
  (active TMDS at `[6]`).
- **`dvi_update_scanline_data_dma()`** — Added `bool audio` parameter. When
  `audio=true`, the active TMDS buffer pointer is patched at block `[5]`
  (sync) / `[6]` (non-sync) instead of the non-audio positions.
- New helper timing functions: `dvi_timing_get_pixel_clock()`,
  `dvi_timing_get_pixels_per_frame()`, `dvi_timing_get_pixels_per_line()`.
- New timing mode: `dvi_timing_720x480p_60hz`.

### IRQ handler additions (`dvi.c`)

- **`dvi_enable_data_island()`** — Rebuilds all 5 DMA lists (vblank_sync,
  vblank_nosync, active, error, active_blank) with audio-capable versions and
  patches data island stream pointers into each list.
- **`dvi_update_data_packet()`** — Called at end of every IRQ when data island
  is enabled. Decides per-scanline whether to emit: AVI InfoFrame, Audio
  InfoFrame, Audio Clock Regeneration packet, audio sample packet, or null
  packet.
- **`dvi_update_data_island_ptr()`** — Patches the `read_addr` of block `[1]`
  (sync lane) or block `[2]` (non-sync lanes) in a DMA list to point to the
  current `next_data_stream` double-buffer slot.

### New `dvi_inst` fields (`dvi.h`)

```c
// Audio / Data Island state
bool data_island_is_enabled;
bool scanline_is_enabled;
data_island_stream_t next_data_stream;
audio_ring_t audio_ring;
data_packet_t avi_info_frame;
data_packet_t audio_clock_regeneration;
data_packet_t audio_info_frame;
int audio_freq;
uint samples_per_frame;
uint64_t samples_per_line16;   // fixed-point 16.48 audio sample accumulator
int64_t  audio_sample_pos;
uint audio_frame_count;
uint left_audio_sample_count;

// Blank line settings
dvi_blank_t blank_settings;

// 2-entry in-flight TMDS buffer pipeline
uint32_t *tmds_buf_release_next;
uint32_t *tmds_buf_release;
```

### Public audio API (`dvi.h`)

```c
// Set ring buffer for audio samples (call before dvi_set_audio_freq)
void dvi_audio_sample_buffer_set(struct dvi_inst *inst, audio_sample_t *buffer, int size);

// Configure audio and enable HDMI audio output
// 128 * audio_freq = pixel_clock * N / CTS
void dvi_set_audio_freq(struct dvi_inst *inst, int audio_freq, int cts, int n);

// Get pointer to blank settings (top/bottom blank lines)
static inline dvi_blank_t *dvi_get_blank_settings(struct dvi_inst *inst);
```

---

## Changes Made During the RP2350 Port

### 1. `.tcr` → `.dbg_tcr` (Critical RP2350 Hardware Fix)

**File:** `software/libdvi/dvi.c` — IRQ handler

```c
// RP2040 (rp2240):
while (dma_debug_hw->ch[inst->dma_cfg[i].chan_data].tcr
       != inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD)

// RP2350 (rp2350):
while (dma_debug_hw->ch[inst->dma_cfg[i].chan_data].dbg_tcr
       != inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD)
```

**Why:** On RP2040, `TCR` (Transfer Count Register) is a live decrementing
counter. Polling `tcr == h_active_pixels/2` catches the brief moment when the
active-pixel DMA block has just been loaded (the counter is freshly reloaded).
On RP2350, the architecture changed: `DBG_TCR` is a **dedicated debug
read-back of the reload value** and remains stable at the reload value for the
entire duration of the active pixel transfer, making the wait condition
reliably correct throughout the active period instead of relying on catching a
transient value.

**This is the most critical RP2350-specific fix.** Without it, the IRQ handler
may advance to the next scanline DMA list before the current transfer is
finished, causing visual corruption.

---

### 2. `tmds_buf_release[2]` → `tmds_buf_release` + `tmds_buf_release_next`

**File:** `software/libdvi/dvi.h`, `software/libdvi/dvi.c`

```c
// rp2240: array-based 2-entry pipeline
uint32_t *tmds_buf_release[2];
// ...
inst->tmds_buf_release[1] = inst->tmds_buf_release[0];
inst->tmds_buf_release[0] = NULL;

// rp2350: named fields (functionally identical)
uint32_t *tmds_buf_release_next;
uint32_t *tmds_buf_release;
// ...
inst->tmds_buf_release = inst->tmds_buf_release_next;
inst->tmds_buf_release_next = NULL;
```

**Why:** The 2-cycle pipeline ensures that after a TMDS buffer is enqueued via
the control block for the last time, two IRQ cycles pass before it is returned
to the free queue (one for the control block load, one for the data transfer
completion). The rename improves readability with no behavioral change.

---

### 3. Type precision improvements

**File:** `software/libdvi/dvi.h`

```c
// rp2240 (may overflow at high sample rates):
int samples_per_frame;
int samples_per_line16;
int audio_sample_pos;
int audio_frame_count;

// rp2350 (correct precision):
uint     samples_per_frame;
uint64_t samples_per_line16;   // was int → overflow fix
int64_t  audio_sample_pos;     // was int → overflow fix
uint     audio_frame_count;
```

**Why:** `samples_per_line16` is a 16.x fixed-point accumulator computed as
`audio_freq * pixels_per_line * 65536 / pixel_clock`. For a 44100 Hz audio
rate with a 25.2 MHz pixel clock and 800 pixels per line:
`44100 * 800 * 65536 / 25200000 ≈ 92,137,142` — which overflows `int`
(max ~2.1 billion for typical values but can overflow at higher clock rates or
in 32-bit fixed-point arithmetic). Using `uint64_t` eliminates this class of
bug entirely.

---

### 4. `dvi_callback_t` signature change

**File:** `software/libdvi/dvi.h`

```c
// rp2240: callback receives scanline number
typedef void (*dvi_callback_t)(uint);

// rp2350: no argument (simpler)
typedef void (*dvi_callback_t)(void);
```

**Migration note:** Any application code using `scanline_callback` that
accepted a `uint` argument must be updated to a `void` signature. The scanline
number can be tracked independently using `inst->timing_state.v_ctr` if
needed.

---

### 5. `dvi_unregister_irqs_this_core()` added to rp2350

**File:** `software/libdvi/dvi.c`, `software/libdvi/dvi.h`

This function existed in rp2240 but was missing from the initial rp2350 port.
It has been added in this migration:

```c
// Unregisters DVI IRQ callbacks for this core and returns any in-flight TMDS
// buffers to the free queue. Call before dvi_stop() when tearing down DVI.
void dvi_unregister_irqs_this_core(struct dvi_inst *inst, uint irq_num);
```

**Why:** When stopping DVI output (e.g., to switch video modes or reinitialise
the output), `dvi_stop()` alone aborts DMA but does not clean up the IRQ
handler or recover the 1–2 TMDS buffers that may be in-flight in the release
pipeline. Without this function, `q_tmds_free` can be permanently short of
buffers after a stop/restart cycle, eventually causing `q_tmds_free` to run
empty and the TMDS encode worker to block forever.

The rp2350 implementation handles the named-field pipeline:

```c
void dvi_unregister_irqs_this_core(struct dvi_inst *inst, uint irq_num) {
    irq_set_enabled(irq_num, false);
    if (irq_num == DMA_IRQ_0)
        irq_remove_handler(DMA_IRQ_0, dvi_dma0_irq);
    else
        irq_remove_handler(DMA_IRQ_1, dvi_dma1_irq);
    // Return any in-flight TMDS buffers back to the free queue
    if (inst->tmds_buf_release) {
        queue_try_add_u32(&inst->q_tmds_free, &inst->tmds_buf_release);
        inst->tmds_buf_release = NULL;
    }
    if (inst->tmds_buf_release_next) {
        queue_try_add_u32(&inst->q_tmds_free, &inst->tmds_buf_release_next);
        inst->tmds_buf_release_next = NULL;
    }
}
```

---

### 6. `blank_settings` initialisation in `dvi_audio_init()`

**File:** `software/libdvi/dvi.c`

```c
// rp2350 only — explicitly zeroes blank_settings in dvi_audio_init():
inst->blank_settings.top    = 0;
inst->blank_settings.bottom = 0;
inst->blank_settings.left   = 0;
inst->blank_settings.right  = 0;
```

**Why:** In rp2240 `blank_settings` was zeroed implicitly (the struct is in
BSS for global variables). Explicit initialisation makes `dvi_audio_init()`
safe for stack-allocated or dynamically-allocated `dvi_inst` structs.

---

### 7. `dvi_is_started()` inline helper added to rp2350

**File:** `software/libdvi/dvi.h`

```c
// Reports DVI status: true = active, false = inactive.
static inline bool dvi_is_started(struct dvi_inst *inst) {
    return inst->dvi_started;
}
```

**Why:** `dvi_started` was a public field in rp2240 but accessed directly.
The inline accessor is idiomatic C and allows the implementation to change
without breaking user code.

---

### 8. C++ compatibility guards added to `dvi.h`

**File:** `software/libdvi/dvi.h`

```c
#ifdef __cplusplus
extern "C" {
#endif
// ... all declarations ...
#ifdef __cplusplus
}
#endif
```

**Why:** Allows `dvi.h` to be included from C++ translation units (e.g., when
using the Arduino framework or C++ wrappers).

---

### 9. RP2350 extended GPIO support in `sprite_bounce_audio`

**File:** `software/apps/sprite_bounce_audio/main.c`

```c
#if PICO_PIO_USE_GPIO_BASE
pio_set_gpio_base(DVI_DEFAULT_SERIAL_CONFIG.pio, 16);
#endif
```

**Why:** The RP2350 has 48 GPIOs (vs. 30 on RP2040). The PIO state machines
have a "GPIO base" register that offsets which physical GPIO pins they address,
allowing TMDS outputs to be mapped to the upper GPIO range (pins 16–47). This
is a no-op on boards that use the lower GPIO range.

---

## Summary of All Differences

| Feature | rp2240 | rp2350 | Status |
|---------|--------|--------|--------|
| DMA wait register | `.tcr` | `.dbg_tcr` | ✅ RP2350 required fix |
| Buffer release fields | `release[2]` array | `release` + `release_next` | ✅ Equivalent, better named |
| `samples_per_line16` type | `int` | `uint64_t` | ✅ Overflow fix |
| `audio_sample_pos` type | `int` | `int64_t` | ✅ Overflow fix |
| `dvi_callback_t` argument | `uint` (scanline) | `void` | ⚠️ Breaking API change |
| `dvi_unregister_irqs_this_core` | ✅ Present | ✅ Added in migration | ✅ Now present |
| `dvi_is_started()` | ✅ Present | ✅ Added in migration | ✅ Now present |
| `blank_settings` init | Implicit | Explicit | ✅ Improvement |
| `__cplusplus` guards | ❌ | ✅ | ✅ Improvement |
| Extended GPIO (`PICO_PIO_USE_GPIO_BASE`) | ❌ | ✅ | ✅ RP2350-specific |
| Audio DMA lists (all 5 variants) | ✅ | ✅ | ✅ Identical logic |
| `dvi_wait_for_valid_line()` | ✅ | ✅ | ✅ Present in both |

---

## Application Migration Guide

If migrating an application from `PicoDVI_rp2240` to `picoDVI_rp2350`:

### 1. Update scanline callback signature

```c
// Old (rp2240):
void my_scanline_callback(uint scanline_num) { ... }

// New (rp2350):
void my_scanline_callback(void) {
    uint scanline_num = dvi_inst.timing_state.v_ctr;
    ...
}
```

### 2. Add GPIO base call for boards with upper-range TMDS pins

```c
dvi_init(&dvi_inst, next_striped_spin_lock_num(), next_striped_spin_lock_num());
#if PICO_PIO_USE_GPIO_BASE
pio_set_gpio_base(DVI_DEFAULT_SERIAL_CONFIG.pio, 16);
#endif
dvi_register_irqs_this_core(&dvi_inst, DMA_IRQ_0);
```

### 3. Audio setup is identical

```c
// Same API in both rp2240 and rp2350:
static audio_sample_t audio_buffer[AUDIO_BUFFER_SIZE];
dvi_audio_sample_buffer_set(&dvi_inst, audio_buffer, AUDIO_BUFFER_SIZE);
dvi_set_audio_freq(&dvi_inst, 44100, 28000, 6272);
```

### 4. Use `dvi_unregister_irqs_this_core()` before stop/restart

```c
// Old (rp2240 had this; initial rp2350 port was missing it):
dvi_unregister_irqs_this_core(&dvi_inst, DMA_IRQ_0);
dvi_stop(&dvi_inst);
// ... reinitialise ...
dvi_init(&dvi_inst, ...);
dvi_register_irqs_this_core(&dvi_inst, DMA_IRQ_0);
dvi_start(&dvi_inst);
```
