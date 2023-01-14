/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef REGIONS_H
#define REGIONS_H
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "scene.h"

struct region {
	struct box box;
	struct box ispercentage;

	char *name;

	xmlNode *node;
	struct wl_list link;
};

struct wl_list *regions_init(const char *filename);
void regions_finish(void);
void regions_save(const char *filename);

#endif /* REGIONS_H */
