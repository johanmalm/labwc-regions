// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include "regions.h"
#include "scene.h"
#include "util.h"

static xmlDoc *doc;
static struct wl_list regions;
static bool in_region;
static struct region *current_region;

/* Return xpath style nodename, e.g <a><b></b></a> becomes /a/b */
static char *
nodename(xmlNode *node, char *buf, int len)
{
	if (!node || !node->name) {
		return NULL;
	}

	/* Ignore superflous '/text' in node name */
	if (node->parent && !strcmp((char *)node->name, "text")) {
		node = node->parent;
	}

	buf += len;
	*--buf = 0;
	len--;

	for (;;) {
		const char *name = (char *)node->name;
		int i = strlen(name);
		while (--i >= 0) {
			unsigned char c = name[i];
			*--buf = tolower(c);
			if (!--len)
				return buf;
		}
		node = node->parent;
		if (!node || !node->name) {
			*--buf = '/';
			return buf;
		}
		*--buf = '/';
		if (!--len)
			return buf;
	}
}

static struct region *
region_create(const char *id)
{
	struct region *region = calloc(1, sizeof(struct region));
	region->id = strdup(id);
	wl_list_insert(regions.prev, &region->link);
	return region;
}

static void
fill_region(char *nodename, char *content)
{
	if (!content) {
		return;
	}
	if (!strcmp(nodename, "/labwc_config/regions/region/id")) {
		current_region = region_create(content);
	} else if (!current_region) {
		LOG(LOG_ERROR, "expect <region id=\"\"> element first");
		return;
	} else if (!strcmp(nodename, "/labwc_config/regions/region/x")) {
		current_region->box.x = atoi(content);
	} else if (!strcmp(nodename, "/labwc_config/regions/region/y")) {
		current_region->box.y = atoi(content);
	} else if (!strcmp(nodename, "/labwc_config/regions/region/width")) {
		current_region->box.width = atoi(content);
	} else if (!strcmp(nodename, "/labwc_config/regions/region/height")) {
		current_region->box.height = atoi(content);
	}
}

static void
entry(xmlNode *node, char *nodename, char *content)
{
	if (!nodename) {
		return;
	}
	if (in_region) {
		fill_region(nodename, content);
	}
}

static void
process_node(xmlNode *node)
{
	char *content;
	static char buffer[256];
	char *name;

	content = (char *)node->content;
	if (xmlIsBlankNode(node)) {
		return;
	}
	name = nodename(node, buffer, sizeof(buffer));
	entry(node, name, content);
}

static void xml_tree_walk(xmlNode *node);

static void
traverse(xmlNode *n)
{
	process_node(n);
	for (xmlAttr *attr = n->properties; attr; attr = attr->next) {
		xml_tree_walk(attr->children);
	}
	xml_tree_walk(n->children);
}

static void
xml_tree_walk(xmlNode *node)
{
	for (xmlNode *n = node; n && n->name; n = n->next) {
		if (!strcasecmp((char *)n->name, "comment")) {
			continue;
		}
		if (!strcasecmp((char *)n->name, "region")) {
			in_region = true;
			traverse(n);
			in_region = false;
			continue;
		}
		traverse(n);
	}
}

struct wl_list *
regions_init(const char *filename)
{
	wl_list_init(&regions);

	LIBXML_TEST_VERSION
	xmlKeepBlanksDefault(0);
	xmlIndentTreeOutput = 1;

	if (access(filename, F_OK)) {
		fprintf(stderr, "no file (%s)\n", filename);
	}
	doc = xmlReadFile(filename, NULL, 0);
	if (!doc) {
		LOG(LOG_ERROR, "error parsing config file");
		exit(EXIT_FAILURE);
	}
	xml_tree_walk(xmlDocGetRootElement(doc));
	return &regions;
}

void
regions_finish(void)
{
	struct region *region, *next;
	wl_list_for_each_safe(region, next, &regions, link) {
		free(region->id);
		wl_list_remove(&region->link);
		free(region);
	}
	xmlFreeDoc(doc);
	xmlCleanupParser();
}

