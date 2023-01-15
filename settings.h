/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SETTINGS_H
#define SETTINGS_H
#include "types.h"

struct window;

struct config {
	char filename[4096];
	struct wl_list *regions;
};

void convert_regions_from_percentage_to_pixels(struct window *window);
struct wl_list *settings_init(const char *filename);
void settings_finish(void);
void settings_save(const struct state *state);

#endif /* SETTINGS_H */
