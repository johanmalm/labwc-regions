// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <cairo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include "regions.h"
#include "scene.h"
#include "util.h"
#include "window.h"

#define FONT ("Sans 10")
#define COLOR_BG (0x000000AA)
#define COLOR_FG (0xCCCCCCFF)
#define SCALE (1.0)

static struct wl_list *regions;

static struct {
	int x;
	int y;
	struct region *region;
} grab;

static void
plot_rect(cairo_t *cairo, struct box *box, uint32_t color, bool fill)
{
	double thickness = fill ? 0.0 : 1.0;
	cairo_save(cairo);
	set_source_u32(cairo, color);
	cairo_rectangle(cairo, box->x + thickness / 2.0, box->y + thickness / 2.0,
		box->width - thickness, box->height - thickness);
	cairo_set_line_width(cairo, thickness);
	if (fill) {
		cairo_fill(cairo);
	} else {
		cairo_stroke(cairo);
	}
	cairo_restore(cairo);
}

void
convert_regions_from_pixels_to_percentage(struct state *state, struct wl_list *regions)
{
	int width = state->surface->width;
	int height = state->surface->height;
	struct region *region;
	wl_list_for_each(region, regions, link) {
		if (!region->ispercentage.x) {
			region->box.x *= 100.0;
			region->box.x /= (float)width;
		}
		if (!region->ispercentage.y) {
			region->box.y *= 100.0;
			region->box.y /= (float)height;
		}
		if (!region->ispercentage.width) {
			region->box.width *= 100.0;
			region->box.width /= (float)width;
		}
		if (!region->ispercentage.height) {
			region->box.height *= 100.0;
			region->box.height /= (float)height;
		}
		region->ispercentage = (struct box){ .x = 1, .y = 1, .width = 1, .height = 1 };
	}
}

void
convert_regions_from_percentage_to_pixels(struct state *state, struct wl_list *regions)
{
	int width = state->surface->width;
	int height = state->surface->height;
	struct region *region;
	wl_list_for_each(region, regions, link) {
		if (region->ispercentage.x) {
			region->box.x *= (float)width;;
			region->box.x /= 100.0;
		}
		if (region->ispercentage.y) {
			region->box.y *= (float)height;
			region->box.y /= 100.0;
		}
		if (region->ispercentage.width) {
			region->box.width *= (float)width;
			region->box.width /= 100.0;
		}
		if (region->ispercentage.height) {
			region->box.height *= (float)height;
			region->box.height /= 100.0;
		}
		region->ispercentage = (struct box){ .x = 0, .y = 0, .width = 0, .height = 0 };
	}
}

void
scene_update(cairo_t *cairo, struct state *state)
{
	static bool has_been_converted_from_percentage;
	if (!has_been_converted_from_percentage) {
		/*
		 * scene_update() is never called before the layer-surface is
		 * configured, so at this point the surface has width/height
		 * which is what we need to covert from percentages.
		 */
		convert_regions_from_percentage_to_pixels(state, regions);
		has_been_converted_from_percentage = true;
	}

	/* Clear background */
	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	set_source_u32(cairo, 0x00000000);
	cairo_paint(cairo);
	cairo_restore(cairo);

	/* background */
	struct box box = {
		.width = state->surface->width,
		.height = state->surface->height,
	};
	plot_rect(cairo, &box, COLOR_BG, true);

	/* regions */
	set_source_u32(cairo, COLOR_FG);
	struct region *region;
	wl_list_for_each(region, regions, link) {
		plot_rect(cairo, &region->box, COLOR_FG, false);
		cairo_move_to(cairo, region->box.x + 5, region->box.y + 5);
		render_text(cairo, FONT, SCALE, region->name);
	}
}

void
scene_init(const char *filename)
{
	regions = regions_init(filename);
}

static void
send_signal_to_labwc_pid(int signal)
{
	char *labwc_pid = getenv("LABWC_PID");
	if (!labwc_pid) {
		exit(EXIT_FAILURE);
	}
	int pid = atoi(labwc_pid);
	if (!pid) {
		exit(EXIT_FAILURE);
	}
	kill(pid, signal);
}

void
scene_finish(const char *filename, struct state *state)
{
	char *save_in_pixel_format = getenv("REGIONS_PIXEL_FORMAT");

	if (!save_in_pixel_format) {
		convert_regions_from_pixels_to_percentage(state, regions);
	}
	regions_save(filename, !!save_in_pixel_format);
	regions_finish();
	send_signal_to_labwc_pid(SIGHUP);
}

static bool
box_empty(const struct box *box)
{
	return !box || box->width <= 0 || box->height <= 0;
}

static bool
box_contains_point(const struct box *box, double x, double y)
{
	if (box_empty(box)) {
		return false;
	}
	return x >= box->x && x < box->x + box->width
		&& y >= box->y && y < box->y + box->height;
}

void
scene_handle_cursor_motion(struct state *state, int x, int y)
{
	if (grab.region) {
		grab.region->box.x += x - grab.x;
		grab.region->box.y += y - grab.y;
		grab.x = x;
		grab.y = y;
	}
	surface_damage(state->surface);
}

void
scene_handle_button_pressed(struct state *state, int x, int y)
{
	grab.x = x;
	grab.y = y;

	struct region *region;
	wl_list_for_each(region, regions, link) {
		if (box_contains_point(&region->box, x, y)) {
			grab.region = region;
			return;
		}
	}
}

void
scene_handle_button_released(struct state *state, int x, int y)
{
	if (grab.region) {
		grab.region = NULL;
	} else {
		state->run_display = false;
	}
}

void
scene_handle_key(struct state *state, xkb_keysym_t keysym, uint32_t codepoint)
{
	switch (keysym) {
	case XKB_KEY_Up:
	case XKB_KEY_Down:
	case XKB_KEY_Right:
	case XKB_KEY_Left:
		break;
	case XKB_KEY_Escape:
		state->run_display = false;
		break;
	default:
		break;
	}
	surface_damage(state->surface);
}
