/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SCENE_H
#define SCENE_H
#include <cairo.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

struct window;

void scene_update(cairo_t *cairo, struct window *window);
void scene_init(const char *filename);
void scene_finish(const char *filename, struct window *window);

void scene_handle_cursor_motion(struct window *window, int x, int y);
void scene_handle_button_pressed(struct window *window, int x, int y);
void scene_handle_button_released(struct window *window, int x, int y);
void scene_handle_key(struct window *window, xkb_keysym_t keysym, uint32_t codepoint);

#endif /* SCENE_H */
