#ifndef _DVI_H
#define _DVI_H

#ifdef __cplusplus
extern "C" {
#endif

#define N_TMDS_LANES 3
#define TMDS_SYNC_LANE 0 // blue!

#include "pico/util/queue.h"

#include "dvi_config_defs.h"
#include "dvi_timing.h"
#include "dvi_serialiser.h"
#include "util_queue_u32_inline.h"
#include "data_packet.h"
#include "audio_ring.h"

typedef void (*dvi_callback_t)(void);

struct dvi_inst {
	// Config ---
	const struct dvi_timing *timing;
	struct dvi_lane_dma_cfg dma_cfg[N_TMDS_LANES];
	struct dvi_timing_state timing_state;
	struct dvi_serialiser_cfg ser_cfg;
	// Called in the DMA IRQ once per scanline -- careful with the run time!
	dvi_callback_t scanline_callback;

	// State ---
	struct dvi_scanline_dma_list dma_list_vblank_sync;
	struct dvi_scanline_dma_list dma_list_vblank_nosync;
	struct dvi_scanline_dma_list dma_list_active;
	struct dvi_scanline_dma_list dma_list_error;
	struct dvi_scanline_dma_list dma_list_active_blank;

	// After a TMDS buffer has been enqueue via a control block for the last
	// time, two IRQs must go by before freeing. The first indicates the control
	// block for this buf has been loaded, and the second occurs some time after
	// the actual data DMA transfer has completed.
	uint32_t *tmds_buf_release_next;
	uint32_t *tmds_buf_release;
	// Remember how far behind the source is on TMDS scanlines, so we can output
	// solid colour until they catch up (rather than dying spectacularly)
	uint late_scanline_ctr;

	// Encoded scanlines:
	queue_t q_tmds_valid;
	queue_t q_tmds_free;

	// Either scanline buffers or frame buffers:
	queue_t q_colour_valid;
	queue_t q_colour_free;

	// HDMI Audio / Data Island ---
	bool dvi_started;
	bool data_island_is_enabled;
	bool scanline_is_enabled;
	uint dvi_frame_count;

	// Blank line settings (top/bottom lines output as black)
	dvi_blank_t blank_settings;

	// Data island stream (points into next_data_stream)
	data_island_stream_t next_data_stream;

	// Audio ring buffer
	audio_ring_t audio_ring;

	// HDMI InfoFrame packets
	data_packet_t avi_info_frame;
	data_packet_t audio_clock_regeneration;
	data_packet_t audio_info_frame;

	// Audio timing state
	int audio_freq;
	uint samples_per_frame;
	uint64_t samples_per_line16;
	uint left_audio_sample_count;
	int64_t audio_sample_pos;
	uint audio_frame_count;
};

// Set up data structures and hardware for DVI.
void dvi_init(struct dvi_inst *inst, uint spinlock_tmds_queue, uint spinlock_colour_queue);

// Call this after calling dvi_init(). DVI DMA interrupts will be routed to
// whichever core called this function. Registers an exclusive IRQ handler.
void dvi_register_irqs_this_core(struct dvi_inst *inst, uint irq_num);

// Start actually wiggling TMDS pairs. Call this once you have initialised the
// DVI, have registered the IRQs, and are producing rendered scanlines.
void dvi_start(struct dvi_inst *inst);

// Stop DVI output and disable serialiser.
void dvi_stop(struct dvi_inst *inst);

// TMDS encode worker function: core enters and doesn't leave, but still
// responds to IRQs. Repeatedly pop a scanline buffer from q_colour_valid,
// TMDS encode it, and pass it to the tmds valid queue.
void dvi_scanbuf_main_8bpp(struct dvi_inst *inst);
void dvi_scanbuf_main_16bpp(struct dvi_inst *inst);

// Same as above, but each q_colour_valid entry is a framebuffer
void dvi_framebuf_main_8bpp(struct dvi_inst *inst);
void dvi_framebuf_main_16bpp(struct dvi_inst *inst);

// Audio/HDMI Data Island API ---

// Initialise audio state (called from dvi_init).
void dvi_audio_init(struct dvi_inst *inst);

// Enable HDMI data island mode (called from dvi_set_audio_freq).
void dvi_enable_data_island(struct dvi_inst *inst);

// Update data island DMA list pointer for a given scanline list.
void dvi_update_data_island_ptr(struct dvi_scanline_dma_list *dma_list, data_island_stream_t *stream);

// Set the audio sample ring buffer (must be called before dvi_set_audio_freq).
void dvi_audio_sample_buffer_set(struct dvi_inst *inst, audio_sample_t *buffer, int size);

// Configure audio frequency and enable HDMI audio output.
// audio_freq: audio sample rate in Hz (e.g. 44100)
// cts: Clock Time Stamp for audio clock regeneration
// n:   N value for audio clock regeneration
// 128 * audio_freq = pixel_clock * N / CTS
void dvi_set_audio_freq(struct dvi_inst *inst, int audio_freq, int cts, int n);

// Called from IRQ handler each scanline when data island is enabled.
void dvi_update_data_packet(struct dvi_inst *inst);

// Block until at least one colour buffer is in the valid queue.
void dvi_wait_for_valid_line(struct dvi_inst *inst);

// Get blank settings struct (top/bottom blank lines).
static inline dvi_blank_t *dvi_get_blank_settings(struct dvi_inst *inst) {
	return &inst->blank_settings;
}

#ifdef __cplusplus
}
#endif

#endif
