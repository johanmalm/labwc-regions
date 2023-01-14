/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SETTINGS_H
#define SETTINGS_H
#include "types.h"

struct wl_list *settings_init(const char *filename);
void settings_finish(void);
void settings_save(const char *filename);

#endif /* SETTINGS_H */
