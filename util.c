/*
 * Copied from https://github.com/swaywm/sway/blob/master/common/util.c
 * Copied from https://github.com/swaywm/sway/blob/master/common/cairo.c
 * Copied from https://github.com/swaywm/sway/blob/master/common/pango.c
 * Copied from https://github.com/swaywm/sway/blob/master/client/pool-buffer.c
 * Copied from https://github.com/swaywm/swaylock/blob/master/log.c
 * Copied from https://github.com/swaywm/swaylock/blob/master/loop.c
 * Copied from https://github.com/swaywm/swaylock/blob/master/unicode.c
 *
 * Copyright (C) 2016-2019 Drew DeVault and sway/swaylock developers
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

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <cairo.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "util.h"

static enum log_importance log_importance = LOG_ERROR;

static const char *verbosity_colors[] = {
	[LOG_SILENT] = "",
	[LOG_ERROR ] = "\x1B[1;31m",
	[LOG_INFO  ] = "\x1B[1;34m",
	[LOG_DEBUG ] = "\x1B[1;30m",
};

void log_init(enum log_importance verbosity) {
	if (verbosity < LOG_IMPORTANCE_LAST) {
		log_importance = verbosity;
	}
}

void _swaylock_log(enum log_importance verbosity, const char *fmt, ...) {
	if (verbosity > log_importance) {
		return;
	}

	va_list args;
	va_start(args, fmt);

	// prefix the time to the log message
	struct tm result;
	time_t t = time(NULL);
	struct tm *tm_info = localtime_r(&t, &result);
	char buffer[26];

	// generate time prefix
	strftime(buffer, sizeof(buffer), "%F %T - ", tm_info);
	fprintf(stderr, "%s", buffer);

	unsigned c = (verbosity < LOG_IMPORTANCE_LAST)
		? verbosity : LOG_IMPORTANCE_LAST - 1;

	if (isatty(STDERR_FILENO)) {
		fprintf(stderr, "%s", verbosity_colors[c]);
	}

	vfprintf(stderr, fmt, args);

	if (isatty(STDERR_FILENO)) {
		fprintf(stderr, "\x1B[0m");
	}
	fprintf(stderr, "\n");

	va_end(args);
}

const char *_swaylock_strip_path(const char *filepath) {
	if (*filepath == '.') {
		while (*filepath == '.' || *filepath == '/') {
			++filepath;
		}
	}
	return filepath;
}

struct loop_fd_event {
	void (*callback)(int fd, short mask, void *data);
	void *data;
	struct wl_list link; // struct loop_fd_event::link
};

struct loop_timer {
	void (*callback)(void *data);
	void *data;
	struct timespec expiry;
	bool removed;
	struct wl_list link; // struct loop_timer::link
};

struct loop {
	struct pollfd *fds;
	int fd_length;
	int fd_capacity;

	struct wl_list fd_events; // struct loop_fd_event::link
	struct wl_list timers; // struct loop_timer::link
};

struct loop *loop_create(void) {
	struct loop *loop = calloc(1, sizeof(struct loop));
	if (!loop) {
		LOG(LOG_ERROR, "Unable to allocate memory for loop");
		return NULL;
	}
	loop->fd_capacity = 10;
	loop->fds = malloc(sizeof(struct pollfd) * loop->fd_capacity);
	wl_list_init(&loop->fd_events);
	wl_list_init(&loop->timers);
	return loop;
}

void loop_destroy(struct loop *loop) {
	struct loop_fd_event *event = NULL, *tmp_event = NULL;
	wl_list_for_each_safe(event, tmp_event, &loop->fd_events, link) {
		wl_list_remove(&event->link);
		free(event);
	}
	struct loop_timer *timer = NULL, *tmp_timer = NULL;
	wl_list_for_each_safe(timer, tmp_timer, &loop->timers, link) {
		wl_list_remove(&timer->link);
		free(timer);
	}
	free(loop->fds);
	free(loop);
}

void loop_poll(struct loop *loop) {
	// Calculate next timer in ms
	int ms = INT_MAX;
	if (!wl_list_empty(&loop->timers)) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct loop_timer *timer = NULL;
		wl_list_for_each(timer, &loop->timers, link) {
			int timer_ms = (timer->expiry.tv_sec - now.tv_sec) * 1000;
			timer_ms += (timer->expiry.tv_nsec - now.tv_nsec) / 1000000;
			if (timer_ms < ms) {
				ms = timer_ms;
			}
		}
	}
	if (ms < 0) {
		ms = 0;
	}

	int ret = poll(loop->fds, loop->fd_length, ms);
	if (ret < 0) {
		LOG_ERRNO(LOG_ERROR, "poll failed");
		exit(1);
	}

	// Dispatch fds
	size_t fd_index = 0;
	struct loop_fd_event *event = NULL;
	wl_list_for_each(event, &loop->fd_events, link) {
		struct pollfd pfd = loop->fds[fd_index];

		// Always send these events
		unsigned events = pfd.events | POLLHUP | POLLERR;

		if (pfd.revents & events) {
			event->callback(pfd.fd, pfd.revents, event->data);
		}

		++fd_index;
	}

	// Dispatch timers
	if (!wl_list_empty(&loop->timers)) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct loop_timer *timer = NULL, *tmp_timer = NULL;
		wl_list_for_each_safe(timer, tmp_timer, &loop->timers, link) {
			if (timer->removed) {
				wl_list_remove(&timer->link);
				free(timer);
				continue;
			}

			bool expired = timer->expiry.tv_sec < now.tv_sec ||
				(timer->expiry.tv_sec == now.tv_sec &&
				 timer->expiry.tv_nsec < now.tv_nsec);
			if (expired) {
				timer->callback(timer->data);
				wl_list_remove(&timer->link);
				free(timer);
			}
		}
	}
}

void loop_add_fd(struct loop *loop, int fd, short mask,
		void (*callback)(int fd, short mask, void *data), void *data) {
	struct loop_fd_event *event = calloc(1, sizeof(struct loop_fd_event));
	if (!event) {
		LOG(LOG_ERROR, "Unable to allocate memory for event");
		return;
	}
	event->callback = callback;
	event->data = data;
	wl_list_insert(loop->fd_events.prev, &event->link);

	struct pollfd pfd = {fd, mask, 0};

	if (loop->fd_length == loop->fd_capacity) {
		loop->fd_capacity += 10;
		loop->fds = realloc(loop->fds,
				sizeof(struct pollfd) * loop->fd_capacity);
	}

	loop->fds[loop->fd_length++] = pfd;
}

struct loop_timer *loop_add_timer(struct loop *loop, int ms,
		void (*callback)(void *data), void *data) {
	struct loop_timer *timer = calloc(1, sizeof(struct loop_timer));
	if (!timer) {
		LOG(LOG_ERROR, "Unable to allocate memory for timer");
		return NULL;
	}
	timer->callback = callback;
	timer->data = data;

	clock_gettime(CLOCK_MONOTONIC, &timer->expiry);
	timer->expiry.tv_sec += ms / 1000;

	long int nsec = (ms % 1000) * 1000000;
	if (timer->expiry.tv_nsec + nsec >= 1000000000) {
		timer->expiry.tv_sec++;
		nsec -= 1000000000;
	}
	timer->expiry.tv_nsec += nsec;

	wl_list_insert(&loop->timers, &timer->link);

	return timer;
}

bool loop_remove_fd(struct loop *loop, int fd) {
	size_t fd_index = 0;
	struct loop_fd_event *event = NULL, *tmp_event = NULL;
	wl_list_for_each_safe(event, tmp_event, &loop->fd_events, link) {
		if (loop->fds[fd_index].fd == fd) {
			wl_list_remove(&event->link);
			free(event);

			loop->fd_length--;
			memmove(&loop->fds[fd_index], &loop->fds[fd_index + 1],
					sizeof(struct pollfd) * (loop->fd_length - fd_index));
			return true;
		}
		++fd_index;
	}
	return false;
}

bool loop_remove_timer(struct loop *loop, struct loop_timer *remove) {
	struct loop_timer *timer = NULL, *tmp_timer = NULL;
	wl_list_for_each_safe(timer, tmp_timer, &loop->timers, link) {
		if (timer == remove) {
			timer->removed = true;
			return true;
		}
	}
	return false;
}

static int anonymous_shm_open(void) {
	int retries = 100;

	do {
		// try a probably-unique name
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		pid_t pid = getpid();
		char name[50];
		snprintf(name, sizeof(name), "/labwc-regions-%x-%x",
			(unsigned int)pid, (unsigned int)ts.tv_nsec);

		// shm_open guarantees that O_CLOEXEC is set
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			return fd;
		}

		--retries;
	} while (retries > 0 && errno == EEXIST);

	return -1;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
	struct pool_buffer *buffer = data;
	buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release
};

static struct pool_buffer *create_buffer(struct wl_shm *shm,
		struct pool_buffer *buf, int32_t width, int32_t height,
		uint32_t format) {
	uint32_t stride = width * 4;
	size_t size = stride * height;

	void *data = NULL;
	if (size > 0) {
		int fd = anonymous_shm_open();
		if (fd == -1) {
			return NULL;
		}
		if (ftruncate(fd, size) < 0) {
			close(fd);
			return NULL;
		}
		data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
		buf->buffer = wl_shm_pool_create_buffer(pool, 0,
				width, height, stride, format);
		wl_buffer_add_listener(buf->buffer, &buffer_listener, buf);
		wl_shm_pool_destroy(pool);
		close(fd);
	}

	buf->size = size;
	buf->width = width;
	buf->height = height;
	buf->data = data;
	buf->surface = cairo_image_surface_create_for_data(data,
			CAIRO_FORMAT_ARGB32, width, height, stride);
	buf->cairo = cairo_create(buf->surface);
	buf->pango = pango_cairo_create_context(buf->cairo);
	return buf;
}

void destroy_buffer(struct pool_buffer *buffer) {
	if (buffer->buffer) {
		wl_buffer_destroy(buffer->buffer);
	}
	if (buffer->cairo) {
		cairo_destroy(buffer->cairo);
	}
	if (buffer->surface) {
		cairo_surface_destroy(buffer->surface);
	}
	if (buffer->pango) {
		g_object_unref(buffer->pango);
	}
	if (buffer->data) {
		munmap(buffer->data, buffer->size);
	}
	memset(buffer, 0, sizeof(struct pool_buffer));
}

struct pool_buffer *get_next_buffer(struct wl_shm *shm,
		struct pool_buffer pool[static 2], uint32_t width, uint32_t height) {
	struct pool_buffer *buffer = NULL;

	for (size_t i = 0; i < 2; ++i) {
		if (pool[i].busy) {
			continue;
		}
		buffer = &pool[i];
	}

	if (!buffer) {
		return NULL;
	}

	if (buffer->width != width || buffer->height != height) {
		destroy_buffer(buffer);
	}

	if (!buffer->buffer) {
		if (!create_buffer(shm, buffer, width, height,
					WL_SHM_FORMAT_ARGB8888)) {
			return NULL;
		}
	}
	buffer->busy = true;
	return buffer;
}

uint32_t
parse_color(const char *color)
{
	if (color[0] == '#') {
		++color;
	}
	int len = strlen(color);
	if (len != 6 && len != 8) {
		LOG(LOG_DEBUG, "invalid color %s", color);
		return 0xFFFFFFFF;
	}
	uint32_t res = (uint32_t)strtoul(color, NULL, 16);
	if (strlen(color) == 6) {
		res = (res << 8) | 0xFF;
	}
	return res;
}


void
set_source_u32(cairo_t *cairo, uint32_t color)
{
	cairo_set_source_rgba(cairo,
		(color >> (3*8) & 0xFF) / 255.0,
		(color >> (2*8) & 0xFF) / 255.0,
		(color >> (1*8) & 0xFF) / 255.0,
		(color >> (0*8) & 0xFF) / 255.0);
}

static PangoLayout *
get_pango_layout(cairo_t *cairo, const char *font, const char *text, double scale)
{
	PangoLayout *layout = pango_cairo_create_layout(cairo);
	PangoAttrList *attrs;
	attrs = pango_attr_list_new();
	pango_layout_set_text(layout, text, -1);

	pango_attr_list_insert(attrs, pango_attr_scale_new(scale));
	PangoFontDescription *desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	pango_layout_set_single_paragraph_mode(layout, 1);
	pango_layout_set_attributes(layout, attrs);
	pango_attr_list_unref(attrs);
	pango_font_description_free(desc);
	return layout;
}

void get_text_size(cairo_t *cairo, const char *font, int *width, int *height,
		int *baseline, double scale, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	// Add one since vsnprintf excludes null terminator.
	int length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *buf = malloc(length);
	if (buf == NULL) {
		LOG(LOG_ERROR, "Failed to allocate memory");
		return;
	}
	va_start(args, fmt);
	vsnprintf(buf, length, fmt, args);
	va_end(args);

	PangoLayout *layout = get_pango_layout(cairo, font, buf, scale);
	pango_cairo_update_layout(cairo, layout);
	pango_layout_get_pixel_size(layout, width, height);
	if (baseline) {
		*baseline = pango_layout_get_baseline(layout) / PANGO_SCALE;
	}
	g_object_unref(layout);
	free(buf);
}

void get_text_metrics(const char *font, int *height, int *baseline) {
	cairo_t *cairo = cairo_create(NULL);
	PangoContext *pango = pango_cairo_create_context(cairo);
	PangoFontDescription *description = pango_font_description_from_string(font);
	// When passing NULL as a language, pango uses the current locale.
	PangoFontMetrics *metrics = pango_context_get_metrics(pango, description, NULL);

	*baseline = pango_font_metrics_get_ascent(metrics) / PANGO_SCALE;
	*height = *baseline + pango_font_metrics_get_descent(metrics) / PANGO_SCALE;

	pango_font_metrics_unref(metrics);
	pango_font_description_free(description);
	g_object_unref(pango);
	cairo_destroy(cairo);
}

void
render_text(cairo_t *cairo, const char *font, double scale, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	// Add one since vsnprintf excludes null terminator.
	int length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *buf = malloc(length);
	if (buf == NULL) {
		LOG(LOG_ERROR, "Failed to allocate memory");
		return;
	}
	va_start(args, fmt);
	vsnprintf(buf, length, fmt, args);
	va_end(args);

	PangoLayout *layout = get_pango_layout(cairo, font, buf, scale);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_get_font_options(cairo, fo);
	pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
	cairo_font_options_destroy(fo);
	pango_cairo_update_layout(cairo, layout);
	pango_cairo_show_layout(cairo, layout);
	g_object_unref(layout);
	free(buf);
}
cairo_subpixel_order_t to_cairo_subpixel_order(enum wl_output_subpixel subpixel) {
	switch (subpixel) {
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
		return CAIRO_SUBPIXEL_ORDER_RGB;
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
		return CAIRO_SUBPIXEL_ORDER_BGR;
	case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
		return CAIRO_SUBPIXEL_ORDER_VRGB;
	case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
		return CAIRO_SUBPIXEL_ORDER_VBGR;
	default:
		return CAIRO_SUBPIXEL_ORDER_DEFAULT;
	}
	return CAIRO_SUBPIXEL_ORDER_DEFAULT;
}

cairo_surface_t *cairo_image_surface_scale(cairo_surface_t *image,
		int width, int height) {
	int image_width = cairo_image_surface_get_width(image);
	int image_height = cairo_image_surface_get_height(image);

	cairo_surface_t *new =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	cairo_t *cairo = cairo_create(new);
	cairo_scale(cairo, (double)width / image_width,
			(double)height / image_height);
	cairo_set_source_surface(cairo, image, 0, 0);

	cairo_paint(cairo);
	cairo_destroy(cairo);
	return new;
}

size_t utf8_chsize(uint32_t ch) {
	if (ch < 0x80) {
		return 1;
	} else if (ch < 0x800) {
		return 2;
	} else if (ch < 0x10000) {
		return 3;
	}
	return 4;
}

size_t utf8_encode(char *str, uint32_t ch) {
	size_t len = 0;
	uint8_t first;

	if (ch < 0x80) {
		first = 0;
		len = 1;
	} else if (ch < 0x800) {
		first = 0xc0;
		len = 2;
	} else if (ch < 0x10000) {
		first = 0xe0;
		len = 3;
	} else {
		first = 0xf0;
		len = 4;
	}

	for (size_t i = len - 1; i > 0; --i) {
		str[i] = (ch & 0x3f) | 0x80;
		ch >>= 6;
	}

	str[0] = ch | first;
	return len;
}


static const struct {
	uint8_t mask;
	uint8_t result;
	int octets;
} sizes[] = {
	{ 0x80, 0x00, 1 },
	{ 0xE0, 0xC0, 2 },
	{ 0xF0, 0xE0, 3 },
	{ 0xF8, 0xF0, 4 },
	{ 0xFC, 0xF8, 5 },
	{ 0xFE, 0xF8, 6 },
	{ 0x80, 0x80, -1 },
};

int utf8_size(const char *s) {
	uint8_t c = (uint8_t)*s;
	for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
		if ((c & sizes[i].mask) == sizes[i].result) {
			return sizes[i].octets;
		}
	}
	return -1;
}
