/*
 * Copied from https://github.com/swaywm/sway - util.h, cairo_util.h, pango.h, pool-buffer.h
 * Copied from https://github.com/swaywm/swaylock - log.h, loop.h, unicode.h
 *
 * Copyright (C) 2016-2019 Drew DeVault
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef UTIL_H
#define UTIL_H
#include <cairo.h>
#include <errno.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>

enum log_importance {
	LOG_SILENT = 0,
	LOG_ERROR = 1,
	LOG_INFO = 2,
	LOG_DEBUG = 3,
	LOG_IMPORTANCE_LAST,
};

void log_init(enum log_importance verbosity);

#ifdef __GNUC__
#define _ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _ATTRIB_PRINTF(start, end)
#endif

void _swaylock_log(enum log_importance verbosity, const char *format, ...)
	_ATTRIB_PRINTF(2, 3);

const char *_swaylock_strip_path(const char *filepath);

#define LOG(verb, fmt, ...) \
	_swaylock_log(verb, "[%s:%d] " fmt, _swaylock_strip_path(__FILE__), \
			__LINE__, ##__VA_ARGS__)

#define LOG_ERRNO(verb, fmt, ...) \
	LOG(verb, fmt ": %s", ##__VA_ARGS__, strerror(errno))

uint32_t parse_color(const char *color);
void set_source_u32(cairo_t *cairo, uint32_t color);
cairo_subpixel_order_t to_cairo_subpixel_order(enum wl_output_subpixel subpixel);
cairo_surface_t *cairo_image_surface_scale(cairo_surface_t *image,
		int width, int height);
void get_text_size(cairo_t *cairo, const char *font, int *width, int *height,
		int *baseline, double scale, const char *fmt, ...);
void get_text_metrics(const char *font, int *height, int *baseline);
void render_text(cairo_t *cairo, const char *font,
		double scale, const char *fmt, ...);

struct loop;
struct loop_timer;
struct loop *loop_create(void);
void loop_destroy(struct loop *loop);
void loop_poll(struct loop *loop);
void loop_add_fd(struct loop *loop, int fd, short mask,
		void (*func)(int fd, short mask, void *data), void *data);
struct loop_timer *loop_add_timer(struct loop *loop, int ms,
		void (*callback)(void *data), void *data);
bool loop_remove_fd(struct loop *loop, int fd);
bool loop_remove_timer(struct loop *loop, struct loop_timer *timer);

struct pool_buffer {
	struct wl_buffer *buffer;
	cairo_surface_t *surface;
	cairo_t *cairo;
	PangoContext *pango;
	uint32_t width, height;
	void *data;
	size_t size;
	bool busy;
};

struct pool_buffer *get_next_buffer(struct wl_shm *shm,
		struct pool_buffer pool[static 2], uint32_t width, uint32_t height);
void destroy_buffer(struct pool_buffer *buffer);

#define UTF8_MAX_SIZE 4
#define UTF8_INVALID 0x80
uint32_t utf8_decode(const char **str);
size_t utf8_encode(char *str, uint32_t ch);
int utf8_size(const char *str);
size_t utf8_chsize(uint32_t ch);

#endif /* UTIL_H */
