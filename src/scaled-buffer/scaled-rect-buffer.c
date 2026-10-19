// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "scaled-buffer/scaled-rect-buffer.h"
#include <assert.h>
#include <string.h>
#include <wlr/util/log.h>
#include "buffer.h"
#include "common/graphic-helpers.h"
#include "common/mem.h"
#include "scaled-buffer/scaled-buffer.h"

static struct lab_data_buffer *
_create_buffer(struct scaled_buffer *scaled_buffer, double scale)
{
	struct scaled_rect_buffer *self = scaled_buffer->data;

	if (self->width <= 0 || self->height <= 0) {
		return NULL;
	}

	struct lab_data_buffer *buffer = buffer_create_cairo(
		self->width, self->height, scale);
	if (!buffer) {
		wlr_log(WLR_ERROR, "Failed to create rect buffer");
		return NULL;
	}

	cairo_t *cairo = cairo_create(buffer->surface);
	set_cairo_color(cairo, self->color);
	cairo_paint(cairo);
	cairo_destroy(cairo);
	return buffer;
}

static void
_destroy(struct scaled_buffer *scaled_buffer)
{
	struct scaled_rect_buffer *self = scaled_buffer->data;
	scaled_buffer->data = NULL;
	free(self);
}

static bool
_equal(struct scaled_buffer *scaled_buffer_a,
		struct scaled_buffer *scaled_buffer_b)
{
	struct scaled_rect_buffer *a = scaled_buffer_a->data;
	struct scaled_rect_buffer *b = scaled_buffer_b->data;

	return a->width == b->width
		&& a->height == b->height
		&& !memcmp(a->color, b->color, sizeof(a->color));
}

static const struct scaled_buffer_impl impl = {
	.create_buffer = _create_buffer,
	.destroy = _destroy,
	.equal = _equal,
};

/* Public API */
struct scaled_rect_buffer *
scaled_rect_buffer_create(struct wlr_scene_tree *parent)
{
	assert(parent);
	struct scaled_rect_buffer *self = znew(*self);
	struct scaled_buffer *scaled_buffer = scaled_buffer_create(
		parent, &impl, /* drop_buffer */ true);
	if (!scaled_buffer) {
		free(self);
		return NULL;
	}

	scaled_buffer->data = self;
	self->scaled_buffer = scaled_buffer;
	self->scene_buffer = scaled_buffer->scene_buffer;
	return self;
}

void
scaled_rect_buffer_update(struct scaled_rect_buffer *self,
		int width, int height, const float *color)
{
	assert(self);
	assert(color);

	if (width < 0 || height < 0) {
		return;
	}

	if (self->width == width && self->height == height
			&& !memcmp(self->color, color, sizeof(self->color))) {
		return;
	}

	self->width = width;
	self->height = height;
	memcpy(self->color, color, sizeof(self->color));

	/* Calculate the size of the buffer and request re-rendering */
	scaled_buffer_request_update(self->scaled_buffer,
		self->width, self->height);
}
