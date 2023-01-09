/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef REGIONS_H
#define REGIONS_H
#include "scene.h"

struct region {
	struct box box;
	char *name;
	struct wl_list link;
};

struct wl_list *regions_init(const char *filename);
void regions_finish(void);

#endif /* REGIONS_H */
