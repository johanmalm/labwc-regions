/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WINDOW_H
#define WINDOW_H
#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include "util.h"

struct loop_timer;
struct zwlr_layer_shell_v1;

struct window {
	struct seat *seat;

	bool run_display;
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct wl_list outputs;
	struct surface *surface;

	struct loop *eventloop;
	struct loop_timer *hover_timer;
	struct zwlr_layer_shell_v1 *layer_shell;

	void *data;
};

struct output {
	struct window *window;

	char *name;
	struct wl_output *wl_output;
	int32_t scale;
	enum wl_output_subpixel subpixel;

	struct wl_list link; /* window.outputs */
};

enum pointer_event_mask {
	POINTER_EVENT_ENTER = 1 << 0,
	POINTER_EVENT_LEAVE = 1 << 1,
	POINTER_EVENT_MOTION = 1 << 2,
	POINTER_EVENT_BUTTON = 1 << 3,
	POINTER_EVENT_AXIS = 1 << 4,
	POINTER_EVENT_AXIS_SOURCE = 1 << 5,
	POINTER_EVENT_AXIS_STOP = 1 << 6,
	POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};

struct pointer_event {
	uint32_t event_mask;
	wl_fixed_t surface_x, surface_y;
	uint32_t button, state;
	uint32_t time;
	uint32_t serial;
	struct {
		bool valid;
		wl_fixed_t value;
		int32_t discrete;
	} axes[2];
	uint32_t axis_source;
};

struct seat {
	struct window *window;

	struct wl_seat *wl_seat;
	struct wl_pointer *pointer;
	struct wl_surface *cursor_surface;
	struct wl_cursor_theme *cursor_theme;
	struct pointer_event pointer_event;
	int pointer_x;
	int pointer_y;

	struct wl_keyboard *keyboard;
	struct {
		struct xkb_state *state;
		struct xkb_context *context;
		struct xkb_keymap *keymap;
	} xkb;
	int32_t repeat_period_ms;
	int32_t repeat_delay_ms;
	uint32_t repeat_sym;
	uint32_t repeat_codepoint;
	struct loop_timer *repeat_timer;
};

struct surface {
	struct window *window;

	cairo_surface_t *image;
	struct wl_output *wl_output;
	struct wl_surface *surface;
	struct pool_buffer buffers[2];
	bool frame_pending, dirty;
	uint32_t width, height;
	struct zwlr_layer_surface_v1 *layer_surface;
};

void surface_damage(struct surface *surface);
void window_init(struct window *window);
void window_run(struct window *window);
void window_finish(struct window *window);

#endif /* WINDOW_H */
