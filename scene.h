/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SCENE_H
#define SCENE_H
#include <cairo.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

struct fbox {
	double x;
	double y;
	double width;
	double height;
};

struct bbox {
	bool x;
	bool y;
	bool width;
	bool height;
};

struct state;

void scene_update(cairo_t *cairo, struct state *state);
void scene_init(const char *filename);
void scene_finish(const char *filename, struct state *state);

void scene_handle_cursor_motion(struct state *state, int x, int y);
void scene_handle_button_pressed(struct state *state, int x, int y);
void scene_handle_button_released(struct state *state, int x, int y);
void scene_handle_key(struct state *state, xkb_keysym_t keysym, uint32_t codepoint);

#endif /* SCENE_H */
