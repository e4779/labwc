// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "scaled-buffer/scaled-rect-buffer.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "buffer.h"
#include "common/graphic-helpers.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/scene-helpers.h"

/*
 * Round up (with a small margin) to a multiple of 4 logical pixels so that for the usual
 * quarter output scales (1.25, 1.5, 1.75) the snapped buffer box has
 * exactly the same number of device pixels as the backing buffer,
 * allowing wlroots to display it 1:1 without resampling.
 */
static int
round_up_4(int v)
{
	return v > 0 ? (v + 3) & ~3 : 0;
}

/* Replicate wlroots scale_box() edge snapping */
static int
snap(double logical, double scale)
{
	return (int)round(logical * scale);
}

static void
_compute_ink_rect(struct scaled_rect_buffer *self,
		int buf_w, int buf_h, int *inx, int *iny, int *inw, int *inh)
{
	double s = self->active_scale;
	int x0 = 0, y0 = 0, w, h;

	switch (self->mode_x) {
	case SCALED_RECT_SPAN:
		w = buf_w;
		break;
	case SCALED_RECT_SPAN_SNAP:
		w = snap(self->node_x + self->width, s) - snap(self->node_x, s);
		break;
	case SCALED_RECT_HUG_START:
		w = (int)lround(self->width * s);
		x0 = snap(self->edge_x, s) - snap(self->node_x, s);
		break;
	case SCALED_RECT_HUG_END:
		w = (int)lround(self->width * s);
		x0 = snap(self->edge_x, s) - snap(self->node_x, s) - w;
		break;
	case SCALED_RECT_SPAN_BETWEEN: {
		int t = (int)lround(self->height * s);
		int ins0 = (int)lround(self->inset_start * s);
		int ins1 = (int)lround(self->inset_end * s);
		x0 = snap(self->edge_x, s) - t + ins0 - snap(self->node_x, s);
		w = snap(self->edge_x + self->width, s) + t - ins1
			- (x0 + snap(self->node_x, s));
		break; }
	default:
		w = (int)lround(self->width * s);
		x0 = snap(self->edge_x, s) - snap(self->node_x, s) - w;
		break;
	}

	switch (self->mode_y) {
	case SCALED_RECT_SPAN:
		h = buf_h;
		break;
	case SCALED_RECT_SPAN_SNAP:
		h = snap(self->node_y + self->height, s) - snap(self->node_y, s);
		break;
	case SCALED_RECT_HUG_START:
		h = (int)lround(self->height * s);
		y0 = snap(self->edge_y, s) - snap(self->node_y, s);
		break;
	case SCALED_RECT_HUG_END:
		h = (int)lround(self->height * s);
		y0 = snap(self->edge_y, s) - snap(self->node_y, s) - h;
		break;
	case SCALED_RECT_SPAN_BETWEEN: {
		int t = (int)lround(self->width * s);
		y0 = snap(self->edge_y, s) - t - snap(self->node_y, s);
		h = snap(self->edge_y + self->height, s) + t - (y0 + snap(self->node_y, s));
		break; }
	default:
		h = (int)lround(self->height * s);
		y0 = snap(self->edge_y, s) - snap(self->node_y, s) - h;
		break;
	}

	/* Clamp into the buffer (safety; geometry should always fit) */
	if (w > buf_w) {
		w = buf_w;
	}
	if (h > buf_h) {
		h = buf_h;
	}
	x0 = MAX(0, MIN(x0, buf_w - w));
	y0 = MAX(0, MIN(y0, buf_h - h));

	*inx = x0;
	*iny = y0;
	*inw = w;
	*inh = h;
}

static void
_render(struct scaled_rect_buffer *self)
{
	if (self->active_scale <= 0 || self->total_width <= 0 || self->total_height <= 0) {
		return;
	}

	double s = self->active_scale;
	int buf_w = snap(self->node_x + self->total_width, s) - snap(self->node_x, s);
	int buf_h = snap(self->node_y + self->total_height, s) - snap(self->node_y, s);

	int inx, iny, inw, inh;
	_compute_ink_rect(self, buf_w, buf_h, &inx, &iny, &inw, &inh);

	if (inx == self->last_ink_x && iny == self->last_ink_y
			&& inw == self->last_ink_w && inh == self->last_ink_h
			&& !memcmp(self->color, self->last_color, sizeof(self->color))) {
		return;
	}

	struct lab_data_buffer *buffer = buffer_create_cairo(
		self->total_width, self->total_height, s);
	if (!buffer) {
		wlr_log(WLR_ERROR, "Failed to create rect buffer");
		return;
	}

	/* Draw in device pixel coordinates: integer positions give crisp edges */
	cairo_surface_set_device_scale(buffer->surface, 1, 1);
	cairo_t *cairo = cairo_create(buffer->surface);
	set_cairo_color(cairo, self->color);
	cairo_rectangle(cairo, inx, iny, inw, inh);
	cairo_fill(cairo);
	cairo_destroy(cairo);

	self->last_ink_x = inx;
	self->last_ink_y = iny;
	self->last_ink_w = inw;
	self->last_ink_h = inh;
	memcpy(self->last_color, self->color, sizeof(self->color));

	wlr_scene_buffer_set_buffer(self->scene_buffer, &buffer->base);
	wlr_scene_buffer_set_dest_size(self->scene_buffer,
		self->total_width, self->total_height);
}

static void
_handle_outputs_update(struct wl_listener *listener, void *data)
{
	struct scaled_rect_buffer *self =
		wl_container_of(listener, self, outputs_update);
	struct wlr_scene_outputs_update_event *event = data;
	double max_scale = 0;

	for (size_t i = 0; i < event->size; i++) {
		max_scale = MAX(max_scale, event->active[i]->output->scale);
	}
	if (max_scale && self->active_scale != max_scale) {
		self->active_scale = max_scale;
		/* Force re-render: reset last state */
		self->last_ink_w = -1;
		_render(self);
	}
}

static void
_handle_node_destroy(struct wl_listener *listener, void *data)
{
	struct scaled_rect_buffer *self =
		wl_container_of(listener, self, destroy);

	wl_list_remove(&self->outputs_update.link);
	wl_list_remove(&self->destroy.link);
	free(self);
}

/* Public API */
struct scaled_rect_buffer *
scaled_rect_buffer_create(struct wlr_scene_tree *parent)
{
	assert(parent);
	struct scaled_rect_buffer *self = znew(*self);

	self->scene_buffer = lab_wlr_scene_buffer_create(parent, NULL);
	if (!self->scene_buffer) {
		free(self);
		return NULL;
	}

	self->outputs_update.notify = _handle_outputs_update;
	wl_signal_add(&self->scene_buffer->events.outputs_update,
		&self->outputs_update);
	self->destroy.notify = _handle_node_destroy;
	wl_signal_add(&self->scene_buffer->node.events.destroy,
		&self->destroy);

	return self;
}

void
scaled_rect_buffer_update(struct scaled_rect_buffer *self,
		int width, int height, const float *color,
		enum scaled_rect_axis mode_x, enum scaled_rect_axis mode_y,
		double node_x, double node_y, double edge_x, double edge_y,
		int inset_start, int inset_end)
{
	assert(self);
	assert(color);

	if (width < 0 || height < 0) {
		return;
	}

	int total_width = round_up_4(width + 4);
	int total_height = round_up_4(height + 4);

	if (self->width == width && self->height == height
			&& self->total_width == total_width
			&& self->total_height == total_height
			&& self->mode_x == mode_x && self->mode_y == mode_y
			&& self->node_x == node_x && self->node_y == node_y
			&& self->edge_x == edge_x && self->edge_y == edge_y
			&& self->inset_start == inset_start
			&& self->inset_end == inset_end
			&& !memcmp(self->color, color, sizeof(self->color))) {
		return;
	}

	self->width = width;
	self->height = height;
	self->total_width = total_width;
	self->total_height = total_height;
	self->mode_x = mode_x;
	self->mode_y = mode_y;
	self->node_x = node_x;
	self->node_y = node_y;
	self->edge_x = edge_x;
	self->edge_y = edge_y;
	self->inset_start = inset_start;
	self->inset_end = inset_end;
	memcpy(self->color, color, sizeof(self->color));

	/*
	 * Bootstrap: a scene buffer without buffer and dest size has zero
	 * size and thus never overlaps an output, so we would never receive
	 * an outputs_update event. Render at scale 1 until then.
	 */
	if (self->active_scale <= 0) {
		self->active_scale = 1;
	}
	_render(self);
}

void
scaled_rect_buffer_set_color(struct scaled_rect_buffer *self, const float *color)
{
	assert(self);
	assert(color);

	if (!memcmp(self->color, color, sizeof(self->color))) {
		return;
	}
	memcpy(self->color, color, sizeof(self->color));
	_render(self);
}
