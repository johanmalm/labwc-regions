/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TYPES_H
#define TYPES_H
#include <libxml/parser.h>

struct dbox {
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

struct region {
	struct dbox dbox;
	struct bbox ispercentage;

	char *name;

	xmlNode *node;
	struct wl_list link;
};

#endif /* TYPES_H */
