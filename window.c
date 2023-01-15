// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <cairo.h>
#include <getopt.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include "scene.h"
#include "settings.h"
#include "types.h"
#include "util.h"
#include "window.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static void
surface_destroy(struct surface *surface)
{
	if (surface->layer_surface) {
		zwlr_layer_surface_v1_destroy(surface->layer_surface);
	}
	if (surface->surface) {
		wl_surface_destroy(surface->surface);
	}
	destroy_buffer(&surface->buffers[0]);
	destroy_buffer(&surface->buffers[1]);
	free(surface);
}

static bool
surface_is_configured(struct surface *surface)
{
	return (surface->width && surface->height);
}

void
render_frame(struct surface *surface)
{
	struct window *window = surface->window;

	if (!surface_is_configured(surface)) {
		return;
	}
	struct pool_buffer *buffer = get_next_buffer(window->shm,
		surface->buffers, surface->width, surface->height);
	if (!buffer) {
		return;
	}

	cairo_t *cairo = buffer->cairo;
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_identity_matrix(cairo);

	scene_update(cairo, (struct state *)surface->window->data);

	wl_surface_attach(surface->surface, buffer->buffer, 0, 0);
	wl_surface_damage_buffer(surface->surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surface->surface);
}

static void
layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height)
{
	struct surface *surface = data;
	surface->width = width;
	surface->height = height;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	render_frame(surface);
}

static void
layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
	struct surface *surface = data;
	surface_destroy(surface);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void
surface_layer_surface_create(struct surface *surface)
{
	struct window *window = surface->window;

	assert(surface->surface);
	surface->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		window->layer_shell, surface->surface, surface->wl_output,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP, "regions");
	assert(surface->layer_surface);

	zwlr_layer_surface_v1_set_size(surface->layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_anchor(surface->layer_surface,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_exclusive_zone(surface->layer_surface, 0);
	zwlr_layer_surface_v1_set_keyboard_interactivity(
			surface->layer_surface, true);
	zwlr_layer_surface_v1_add_listener(surface->layer_surface,
			&layer_surface_listener, surface);
	wl_surface_commit(surface->surface);
}

static const struct wl_callback_listener surface_frame_listener;

static void
surface_frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	struct surface *surface = data;

	wl_callback_destroy(callback);
	surface->frame_pending = false;
	if (!surface->dirty) {
		return;
	}
	struct wl_callback *_callback = wl_surface_frame(surface->surface);
	wl_callback_add_listener(_callback, &surface_frame_listener, surface);
	surface->frame_pending = true;
	render_frame(surface);
	surface->dirty = false;
}

static const struct wl_callback_listener surface_frame_listener = {
	.done = surface_frame_done,
};

void
surface_damage(struct surface *surface)
{
	if (!surface_is_configured(surface)) {
		return;
	}
	surface->dirty = true;
	if (surface->frame_pending) {
		return;
	}
	struct wl_callback *callback = wl_surface_frame(surface->surface);
	wl_callback_add_listener(callback, &surface_frame_listener, surface);
	surface->frame_pending = true;
	wl_surface_commit(surface->surface);
}

static void
handle_wl_output_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t physical_width,
		int32_t physical_height, int32_t subpixel, const char *make,
		const char *model, int32_t transform)
{
	struct output *output = data;
	output->subpixel = subpixel;
	if (output->window->run_display) {
		surface_damage(output->window->surface);
	}
}

static void
handle_wl_output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
		int32_t width, int32_t height, int32_t refresh)
{
	/* nop */
}

static void
handle_wl_output_done(void *data, struct wl_output *output)
{
	/* nop */
}

static void
handle_wl_output_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
	struct output *output = data;
	output->scale = factor;
	if (output->window->run_display) {
		surface_damage(output->window->surface);
	}
}

static void
handle_wl_output_name(void *data, struct wl_output *wl_output, const char *name)
{
	struct output *output = data;
	output->name = strdup(name);
}

static void
handle_wl_output_description(void *data, struct wl_output *wl_output,
		const char *description)
{
	/* nop */
}

static struct wl_output_listener output_listener = {
	.geometry = handle_wl_output_geometry,
	.mode = handle_wl_output_mode,
	.done = handle_wl_output_done,
	.scale = handle_wl_output_scale,
	.name = handle_wl_output_name,
	.description = handle_wl_output_description,
};

static void
output_init(struct window *window, struct wl_output *wl_output)
{
	struct output *output = calloc(1, sizeof(struct output));
	output->window = window;
	output->wl_output = wl_output;
	output->scale = 1;
	wl_output_add_listener(output->wl_output, &output_listener, output);
	wl_list_insert(&window->outputs, &output->link);
}

static void
handle_wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size)
{
	struct seat *seat = data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		LOG(LOG_ERROR, "unknown keymap format %d", format);
		exit(EXIT_FAILURE);
	}
	char *map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map_shm == MAP_FAILED) {
		close(fd);
		LOG(LOG_ERROR, "unable to initialize keymap shm");
		exit(EXIT_FAILURE);
	}
	struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
		seat->xkb.context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_shm, size);
	close(fd);

	struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
	xkb_keymap_unref(seat->xkb.keymap);
	xkb_state_unref(seat->xkb.state);
	seat->xkb.keymap = xkb_keymap;
	seat->xkb.state = xkb_state;
}

static void
handle_wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface,
		struct wl_array *keys)
{
	/* nop */
}

static void
handle_wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface)
{
	/* nop */
}

static void
keyboard_repeat(void *data)
{
	struct seat *seat = data;
	struct window *window = seat->window;
	seat->repeat_timer = loop_add_timer(window->eventloop,
		seat->repeat_period_ms, keyboard_repeat, seat);
	scene_handle_key(window, seat->repeat_sym, seat->repeat_codepoint);
}

static void
handle_wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t _state)
{
	struct seat *seat = data;
	struct window *window = seat->window;
	enum wl_keyboard_key_state key_state = _state;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(seat->xkb.state, key + 8);
	uint32_t keycode = key_state == WL_KEYBOARD_KEY_STATE_PRESSED ?  key + 8 : 0;
	uint32_t codepoint = xkb_state_key_get_utf32(seat->xkb.state, keycode);
	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		scene_handle_key(window, sym, codepoint);
	}

	if (seat->repeat_timer) {
		loop_remove_timer(seat->window->eventloop, seat->repeat_timer);
		seat->repeat_timer = NULL;
	}
	bool pressed = key_state == WL_KEYBOARD_KEY_STATE_PRESSED;
	if (pressed && seat->repeat_period_ms > 0) {
		seat->repeat_sym = sym;
		seat->repeat_codepoint = codepoint;
		seat->repeat_timer = loop_add_timer(seat->window->eventloop,
			seat->repeat_delay_ms, keyboard_repeat, seat);
	}
}

static void
handle_wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group)
{
	struct seat *seat = data;
	if (!seat->xkb.state) {
		return;
	}
	xkb_state_update_mask(seat->xkb.state, mods_depressed, mods_latched,
		mods_locked, 0, 0, group);
}

static void
handle_wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay)
{
	struct seat *seat = data;
	if (rate <= 0) {
		seat->repeat_period_ms = -1;
	} else {
		seat->repeat_period_ms = 1000 / rate;
	}
	seat->repeat_delay_ms = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = handle_wl_keyboard_keymap,
	.enter = handle_wl_keyboard_enter,
	.leave = handle_wl_keyboard_leave,
	.key = handle_wl_keyboard_key,
	.modifiers = handle_wl_keyboard_modifiers,
	.repeat_info = handle_wl_keyboard_repeat_info,
};

static void
update_cursor(struct seat *seat, uint32_t serial)
{
	struct window *window = seat->window;
	seat->cursor_theme =
		wl_cursor_theme_load(getenv("XCURSOR_THEME"), 24, window->shm);
	struct wl_cursor *cursor =
		wl_cursor_theme_get_cursor(seat->cursor_theme, "left_ptr");
	struct wl_cursor_image *cursor_image = cursor->images[0];
	wl_surface_set_buffer_scale(seat->cursor_surface, 1);
	wl_surface_attach(seat->cursor_surface,
		wl_cursor_image_get_buffer(cursor_image), 0, 0);
	wl_pointer_set_cursor(seat->pointer, serial, seat->cursor_surface,
		cursor_image->hotspot_x, cursor_image->hotspot_y);
	wl_surface_damage_buffer(seat->cursor_surface, 0, 0,
		INT32_MAX, INT32_MAX);
	wl_surface_commit(seat->cursor_surface);
}

static void
handle_wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct seat *seat = data;
	seat->pointer_event.event_mask |= POINTER_EVENT_ENTER;
	seat->pointer_event.serial = serial;
	seat->pointer_event.surface_x = surface_x;
	seat->pointer_event.surface_y = surface_y;
	update_cursor(seat, serial);
}

static void
handle_wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface)
{
	struct seat *seat = data;
	seat->pointer_event.serial = serial;
	seat->pointer_event.event_mask |= POINTER_EVENT_LEAVE;
}

static void
handle_wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct seat *seat = data;
	seat->pointer_event.event_mask |= POINTER_EVENT_MOTION;
	seat->pointer_event.time = time;
	seat->pointer_event.surface_x = surface_x;
	seat->pointer_event.surface_y = surface_y;
}

static void
handle_wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	struct seat *seat = data;
	seat->pointer_event.event_mask |= POINTER_EVENT_BUTTON;
	seat->pointer_event.time = time;
	seat->pointer_event.serial = serial;
	seat->pointer_event.button = button;
	seat->pointer_event.state = state;
}

static void
handle_wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
		uint32_t axis, wl_fixed_t value)
{
	struct seat *seat = data;
	seat->pointer_event.event_mask |= POINTER_EVENT_AXIS;
	seat->pointer_event.time = time;
	seat->pointer_event.axes[axis].valid = true;
	seat->pointer_event.axes[axis].value = value;
}

static void
handle_wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source)
{
	struct seat *seat = data;
	seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_SOURCE;
	seat->pointer_event.axis_source = axis_source;
}

static void
handle_wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis)
{
	struct seat *seat = data;
	seat->pointer_event.time = time;
	seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_STOP;
	seat->pointer_event.axes[axis].valid = true;
}

static void
handle_wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t discrete)
{
	struct seat *seat = data;
	seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
	seat->pointer_event.axes[axis].valid = true;
	seat->pointer_event.axes[axis].discrete = discrete;
}

static void
handle_wl_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
	struct seat *seat = data;
	struct pointer_event *event = &seat->pointer_event;

	if (event->event_mask & POINTER_EVENT_MOTION) {
		seat->pointer_x = wl_fixed_to_int(event->surface_x);
		seat->pointer_y = wl_fixed_to_int(event->surface_y);
		scene_handle_cursor_motion(seat->window,
			seat->pointer_x, seat->pointer_y);
	}

	if (event->event_mask & POINTER_EVENT_BUTTON) {
		int x = seat->pointer_x;
		int y = seat->pointer_y;
		switch (event->state) {
		case WL_POINTER_BUTTON_STATE_PRESSED:
			scene_handle_button_pressed((struct state *)seat->window->data, x, y);
			break;
		case WL_POINTER_BUTTON_STATE_RELEASED:
			scene_handle_button_released(seat->window, x, y);
			break;
		default:
			break;
		}
	}
	memset(event, 0, sizeof(struct pointer_event));
	surface_damage(seat->window->surface);
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = handle_wl_pointer_enter,
	.leave = handle_wl_pointer_leave,
	.motion = handle_wl_pointer_motion,
	.button = handle_wl_pointer_button,
	.axis = handle_wl_pointer_axis,
	.frame = handle_wl_pointer_frame,
	.axis_source = handle_wl_pointer_axis_source,
	.axis_stop = handle_wl_pointer_axis_stop,
	.axis_discrete = handle_wl_pointer_axis_discrete,
};

static void
handle_wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps)
{
	struct seat *seat = data;
	if (seat->pointer) {
		wl_pointer_release(seat->pointer);
		seat->pointer = NULL;
	}
	if (seat->keyboard) {
		wl_keyboard_release(seat->keyboard);
		seat->keyboard = NULL;
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		seat->pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->pointer, &pointer_listener, seat);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		seat->keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(seat->keyboard,
			&keyboard_listener, seat);
	}
}

static void
handle_wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	/* nop */
}

const struct wl_seat_listener seat_listener = {
	.capabilities = handle_wl_seat_capabilities,
	.name = handle_wl_seat_name,
};

static void
seat_init(struct window *window, struct wl_seat *wl_seat)
{
	struct seat *seat = calloc(1, sizeof(struct seat));
	seat->wl_seat = wl_seat;
	seat->window = window;
	window->seat = seat;
	wl_seat_add_listener(wl_seat, &seat_listener, seat);
}

static void
handle_wl_registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	struct window *window = data;
	if (!strcmp(interface, wl_compositor_interface.name)) {
		window->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (!strcmp(interface, wl_shm_interface.name)) {
		window->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (!strcmp(interface, wl_seat_interface.name)) {
		struct wl_seat *wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
		seat_init(window, wl_seat);
	} else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
		window->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct wl_output *wl_output = wl_registry_bind(registry, name, &wl_output_interface, 4);
		output_init(window, wl_output);
	}
}

static void
handle_wl_registry_global_remove(void *data, struct wl_registry *registry,
		uint32_t name)
{
	/* nop */
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_wl_registry_global,
	.global_remove = handle_wl_registry_global_remove,
};

#define DIE_ON(condition, message) do { \
	if ((condition) != 0) { \
		LOG(LOG_ERROR, message); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void
globals_init(struct window *window)
{
	window->registry = wl_display_get_registry(window->display);
	wl_registry_add_listener(window->registry, &registry_listener, window);

	if (wl_display_roundtrip(window->display) < 0) {
		LOG(LOG_ERROR, "wl_display_roundtrip()");
		exit(EXIT_FAILURE);
	}

	DIE_ON(!window->compositor, "no compositor");
	DIE_ON(!window->shm, "no shm");
	DIE_ON(!window->seat, "no seat");
	DIE_ON(!window->layer_shell, "no layer-shell");
}

void
window_init(struct window *window)
{
	wl_list_init(&window->outputs);

	window->display = wl_display_connect(NULL);
	DIE_ON(!window->display, "unable to connect to compositor");

	globals_init(window);

	window->seat->xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	if (wl_display_roundtrip(window->display) < 0) {
		LOG(LOG_ERROR, "wl_display_roundtrip()");
		exit(EXIT_FAILURE);
	}

	window->seat->cursor_surface = wl_compositor_create_surface(window->compositor);

	window->surface = calloc(1, sizeof(struct surface));
	window->surface->window = window;
	window->surface->surface = wl_compositor_create_surface(window->compositor);

	struct output *output;
	wl_list_for_each(output, &window->outputs, link) {
		window->surface->wl_output = output->wl_output;
		break;
	}
	LOG(LOG_INFO, "using output '%s'", output->name);

	/* TODO: add option to create xdg-shell */
	surface_layer_surface_create(window->surface);
}

static void
display_in(int fd, short mask, void *data)
{
	struct window *window = (struct window *)data;
	if (wl_display_dispatch(window->display) == -1) {
		window->run_display = false;
	}
}

void
window_run(struct window *window)
{
	window->eventloop = loop_create();
	loop_add_fd(window->eventloop, wl_display_get_fd(window->display),
		    POLLIN, display_in, window);

	window->run_display = true;
	while (window->run_display) {
		errno = 0;
		if (wl_display_flush(window->display) == -1 && errno != EAGAIN) {
			break;
		}
		loop_poll(window->eventloop);
	}
}

void
window_finish(struct window *window)
{
	surface_destroy(window->surface);

	struct output *output, *next;
	wl_list_for_each_safe(output, next, &window->outputs, link) {
		wl_list_remove(&output->link);
		free(output->name);
		free(output);
	}

	struct seat *seat = window->seat;
	wl_surface_destroy(seat->cursor_surface);
	if (seat->cursor_theme) {
		wl_cursor_theme_destroy(seat->cursor_theme);
	}

	xkb_keymap_unref(seat->xkb.keymap);
	xkb_state_unref(seat->xkb.state);
	xkb_context_unref(seat->xkb.context);

	if (seat->pointer) {
		wl_pointer_destroy(seat->pointer);
	}
	if (seat->keyboard) {
		wl_keyboard_destroy(seat->keyboard);
	}
	wl_seat_destroy(seat->wl_seat);
	free(seat);

	loop_remove_fd(window->eventloop, wl_display_get_fd(window->display));
	loop_destroy(window->eventloop);

	pango_cairo_font_map_set_default(NULL);

	wl_compositor_destroy(window->compositor);
	wl_registry_destroy(window->registry);
	wl_display_disconnect(window->display);
}
