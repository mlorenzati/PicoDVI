#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "pico/sem.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"

#include "micromod.h"
#include "mods.h"
#include "graphics.h"
#include "math.h"

// Pick one:
#define MODE_640x480_60Hz
// #define MODE_800x480_60Hz
// #define MODE_800x600_60Hz
// #define MODE_960x540p_60Hz
// #define MODE_1280x720_30Hz

#if defined(MODE_640x480_60Hz)
// DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

#elif defined(MODE_800x480_60Hz)
#define FRAME_WIDTH 400
#define FRAME_HEIGHT 240
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_800x480p_60hz

#elif defined(MODE_800x600_60Hz)
// DVDD 1.3V, going downhill with a tailwind
#define FRAME_WIDTH 400
#define FRAME_HEIGHT 300
#define VREG_VSEL VREG_VOLTAGE_1_30
#define DVI_TIMING dvi_timing_800x600p_60hz

#elif defined(MODE_960x540p_60Hz)
// DVDD 1.25V (slower silicon may need the full 1.3, or just not work)
// Frame resolution is almost the same as a PSP :)
#define FRAME_WIDTH 480
#define FRAME_HEIGHT 270
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_960x540p_60hz

#elif defined(MODE_1280x720_30Hz)
// 1280x720p 30 Hz (nonstandard)
// DVDD 1.25V (slower silicon may need the full 1.3, or just not work)
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 360
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_1280x720p_30hz

#else
#error "Select a video mode!"
#endif

#define N_BERRIES 10

// Colors 16bits         0brrrrrggggggbbbbb
#define color_black      0b0000000000000000
#define color_dark_gray  0b0001100011100011
#define color_mid_gray   0b1010010100010000
#define color_light_gray 0b1100011000011000
#define color_white      0b1111111111111111
#define color_red        0b1111100000000000
#define color_green      0b0000011111100000
#define color_blue       0b0000000000011111

uint16_t framebuf[FRAME_HEIGHT][FRAME_WIDTH];
static graphic_ctx_t graphic_ctx = {
	.height       = FRAME_HEIGHT,
	.width        = FRAME_WIDTH,
	.video_buffer = framebuf,
	.bppx 		  = rgb_16_565,
	.parent       = NULL
};

const uint color_list[] = {color_red, color_green, color_blue, color_white, color_mid_gray, color_black};

struct dvi_inst dvi0;

void core1_main() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	while (queue_is_empty(&dvi0.q_colour_valid))
		__wfe();
	dvi_start(&dvi0);
	dvi_scanbuf_main_16bpp(&dvi0);
	__builtin_unreachable();
}

//Audio Related
#define AUDIO_FREQUENCY 	44100
#define AUDIO_BUFFER_SIZE   256
audio_sample_t      audio_buffer[2 * AUDIO_BUFFER_SIZE];
struct repeating_timer audio_timer;

// Mod related
volatile uint8_t  current_mod_idx = 0;
volatile uint32_t current_mod_duration = 0;
volatile uint32_t current_mod_samples_played = 0;

void start_mod(uint8_t idx) {
    micromod_initialise((signed char *)mod_list[idx].data, AUDIO_FREQUENCY);
	current_mod_duration = micromod_calculate_song_duration();
	current_mod_samples_played = 0;
}

volatile bool feed_audio = false;
bool __not_in_flash("audio_timer_callback") audio_timer_callback(struct repeating_timer *t) {
    feed_audio = true;
	uint32_t size = get_write_size(&dvi0.audio_ring, false);
	audio_sample_t *audio_ptr = get_write_pointer(&dvi0.audio_ring);
	memset(audio_ptr, 0, size * sizeof(audio_sample_t));
	micromod_get_audio((short *) audio_ptr, size);
	increase_write_pointer(&dvi0.audio_ring, size);
	current_mod_samples_played += size;
    return true;
}

void __not_in_flash("core1_scanline_callback") core1_scanline_callback(uint scanline) {
	void *bufptr  = NULL;
	queue_remove_blocking(&dvi0.q_colour_free, &bufptr);
	bufptr = &framebuf[scanline];

	queue_add_blocking(&dvi0.q_colour_valid, &bufptr);
}

void draw_player() {
	printf("Start rendering\n");

	// Main rect
	draw_rect(&graphic_ctx, 0, 0, graphic_ctx.width, graphic_ctx.height, color_blue);
	draw_rect(&graphic_ctx, 1, 1, graphic_ctx.width - 2, graphic_ctx.height - 2, color_light_gray);

	// Mod Name area
	draw_rect(&graphic_ctx, 3, 3, graphic_ctx.width - 6, 12, color_light_gray);

	// Status area
	draw_rect(&graphic_ctx, 3, graphic_ctx.height - 16, graphic_ctx.width - 6, 12, color_light_gray);
	
	char song_name[24];
	micromod_get_string(0, song_name);
	song_name[16] = 0;

	draw_textf(&graphic_ctx, 5, 5, color_white, color_black, false, "Name:%s |Duration:%ds", song_name, current_mod_duration / AUDIO_FREQUENCY);
	draw_textf(&graphic_ctx, 5, graphic_ctx.height - 14, color_white, color_black, false, "Left:%ds",  (current_mod_duration - current_mod_samples_played) / AUDIO_FREQUENCY);
	
	//draw_line(&graphic_ctx, 0, 0,  319 , 239, color_red);
}

int main() {
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
#ifdef RUN_FROM_CRYSTAL
	// Slow everything down uniformly, so signals are probeable but the code runs
	// identically (note this actually uses the PLL with low feedback and max PD1/PD2)
	set_sys_clock_khz(12000, true);
#else
	// Run system at TMDS bit clock
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
#endif

	setup_default_uart();
	
	#if PICO_PIO_USE_GPIO_BASE
	pio_set_gpio_base(DVI_DEFAULT_SERIAL_CONFIG.pio, 16);
	#endif

	printf("Configuring DVI\n");

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi0.scanline_callback = core1_scanline_callback;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    
    // HDMI Audio related
	dvi_get_blank_settings(&dvi0)->top    = 0;
    dvi_get_blank_settings(&dvi0)->bottom = 0;
    dvi_audio_sample_buffer_set(&dvi0, audio_buffer, AUDIO_BUFFER_SIZE);
    dvi_set_audio_freq(&dvi0, AUDIO_FREQUENCY, 28000, 6272);
    add_repeating_timer_ms(2, audio_timer_callback, NULL, &audio_timer);
	start_mod(current_mod_idx);

	printf("Core 1 start\n");
	multicore_launch_core1(core1_main);

	printf("Allocating scanline buffers\n");
	for (int i = 0; i < TMDS_PREBUFFERING_LINES; ++i) {
		void *bufptr = &framebuf[i];
		queue_add_blocking((void*)&dvi0.q_colour_valid, &bufptr);
	}

	draw_player();

	while (1)
	{
		// if (++ms_2count >= 500) {
		// 	ms_2count = 0;
		// 	// Second event
		// 	draw_textf(&graphic_ctx, 5, graphic_ctx.height - 14, color_white, color_black, false, "Left:%ds",  (current_mod_duration - current_mod_samples_played) / AUDIO_FREQUENCY);

		// 	if (current_mod_samples_played >= current_mod_duration) {
		// 		current_mod_idx = (current_mod_idx + 1) % mod_count;
		// 		start_mod(current_mod_idx);
		// 		draw_player();
		// 	}
		// }
		
		sleep_ms(1000);
	}
	__builtin_unreachable();
}
	
