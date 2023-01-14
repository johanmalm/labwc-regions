/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef REGIONS_H
#define REGIONS_H
#include "types.h"

struct wl_list *regions_init(const char *filename);
void regions_finish(void);
void regions_save(const char *filename);

#endif /* REGIONS_H */
