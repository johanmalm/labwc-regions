// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <cairo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

struct box
get_box_in_pixels(struct state *state, struct region *region)
{
	struct box box;
	box.x = region->box.x * state->surface->width / 100;
	box.y = region->box.y * state->surface->height / 100;
	box.width = region->box.width * state->surface->width / 100;
	box.height = region->box.height * state->surface->height / 100;
	return box;
}

void
scene_update(cairo_t *cairo, struct state *state)
{
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
		struct box box = get_box_in_pixels(state, region);
		plot_rect(cairo, &box, COLOR_FG, false);
		cairo_move_to(cairo, box.x + 5, box.y + 5);
		render_text(cairo, FONT, SCALE, region->id);
	}
}

void
scene_init(const char *filename)
{
	regions = regions_init(filename);
}

void
scene_finish(void)
{
	regions_finish();
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
	struct region *region;
	wl_list_for_each(region, regions, link) {
		if (!box_contains_point(&region->box, x, y)) {
			continue;
		}
		fprintf(stderr, "in box %p\n", region);
	}
	surface_damage(state->surface);
}

void
scene_handle_button_pressed(struct state *state, int x, int y)
{
	fprintf(stderr, "button pressed\n");
}

void
scene_handle_button_released(struct state *state, int x, int y)
{
	state->run_display = false;
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
