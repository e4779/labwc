// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "scaled-buffer/scaled-corner-buffer.h"
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

#define LAB_CORNER_DEG 0.017453292519943295

static int
round_up_4(int v)
{
	return v > 0 ? (v + 3) & ~3 : 0;
}

static int
snap(double logical, double scale)
{
	return (int)round(logical * scale);
}

/*
 * Device pixel column/row of the straight border segments within the
 * buffer, replicating the anchor math of scaled_rect_buffer.
 */
static void
_compute_anchor(struct scaled_corner_buffer *self, int *x0, int *y0)
{
	double s = self->active_scale;
	int t = (int)lround(self->border_width * s);

	*y0 = snap(self->edge_y, s) - snap(self->node_y, s);
	if (self->right) {
		*x0 = snap(self->edge_x, s) - snap(self->node_x, s);
	} else {
		*x0 = snap(self->edge_x, s) - t - snap(self->node_x, s);
	}
}

static void
_render(struct scaled_corner_buffer *self)
{
	if (self->active_scale <= 0 || self->width <= 0 || self->height <= 0) {
		return;
	}

	double s = self->active_scale;
	int x0, y0;
	_compute_anchor(self, &x0, &y0);

	if (self->rendered && x0 == self->last_x0 && y0 == self->last_y0) {
		return;
	}

	struct lab_data_buffer *buffer = buffer_create_cairo(
		self->total_width, self->total_height, s);
	if (!buffer) {
		wlr_log(WLR_ERROR, "Failed to create corner buffer");
		return;
	}

	int t = (int)lround(self->border_width * s);
	double w = self->width;
	double h = self->height;
	double r = self->radius;
	double bw = self->border_width;

	/*
	 * Draw in logical coordinates (like theme.c rounded_rect()) but
	 * rigidly shifted so that the straight segments land on the
	 * anchored device pixels. The shift is computed in device pixels
	 * and applied in logical units via cairo's device scale.
	 */
	double dx_dev, dy_dev = y0;
	if (self->right) {
		dx_dev = x0 - (lround(w * s) - t);
	} else {
		dx_dev = x0;
	}

	int buf_w = snap(self->node_x + self->total_width, s) - snap(self->node_x, s);
	int buf_h = snap(self->node_y + self->total_height, s) - snap(self->node_y, s);
	/* Draw in device pixels: reset the device scale set by
	 * buffer_create_cairo() so integer coordinates are crisp */
	cairo_surface_set_device_scale(buffer->surface, 1, 1);
	cairo_t *cairo = cairo_create(buffer->surface);
	cairo_translate(cairo, dx_dev, dy_dev);
	double wd = w * s;
	double hd = h * s;
	double rd = r * s;
	double bwd = bw * s;

	/* Transparent background */
	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cairo);

	/* Background fill path (same shape as theme.c) */
	cairo_set_line_width(cairo, 0.0);
	cairo_new_sub_path(cairo);
	if (r <= 0) {
		cairo_move_to(cairo, 0, 0);
		cairo_line_to(cairo, wd, 0);
		cairo_line_to(cairo, wd, hd);
		cairo_line_to(cairo, 0, hd);
	} else if (self->right) {
		cairo_arc(cairo, wd - rd, rd, rd, -90 * LAB_CORNER_DEG, 0);
		cairo_line_to(cairo, wd, hd);
		cairo_line_to(cairo, 0, hd);
		cairo_line_to(cairo, 0, 0);
	} else {
		cairo_arc(cairo, rd, rd, rd, 180 * LAB_CORNER_DEG, 270 * LAB_CORNER_DEG);
		cairo_line_to(cairo, wd, 0);
		cairo_line_to(cairo, wd, hd);
		cairo_line_to(cairo, 0, hd);
	}
	cairo_close_path(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source(cairo, self->fill_pattern);
	cairo_fill(cairo);

	/*
	 * Crisp straight border segments: integer device pixel
	 * rectangles, matching scaled_rect_buffer rendering.
	 */
	int Rdev = (int)lround(r * s);
	int seg = MAX(0, Rdev - 1);
	set_cairo_color(cairo, self->border_color);
	if (self->right) {
		cairo_rectangle(cairo, x0, y0 + seg, t, buf_h - y0 - seg);
		int hx = x0 + t - Rdev + 1;
		if (hx > 0) {
			cairo_rectangle(cairo, 0, y0, MIN(hx, buf_w), t);
		}
	} else {
		cairo_rectangle(cairo, x0, y0 + seg, t, buf_h - y0 - seg);
		cairo_rectangle(cairo, x0 + seg, y0, buf_w - x0 - seg, t);
	}
	cairo_fill(cairo);

	/* Arc section */
	if (r > 0) {
		double line_width = MIN(bwd, rd);
		double half_lw = line_width / 2.0;
		cairo_set_line_width(cairo, line_width);
		if (self->right) {
			cairo_move_to(cairo, wd - rd, half_lw);
			cairo_arc(cairo, wd - rd, rd, rd - half_lw,
				-90 * LAB_CORNER_DEG, 0);
		} else {
			cairo_move_to(cairo, half_lw, rd);
			cairo_arc(cairo, rd, rd, rd - half_lw,
				180 * LAB_CORNER_DEG, 270 * LAB_CORNER_DEG);
		}
		cairo_stroke(cairo);
	}

	cairo_destroy(cairo);

	self->last_x0 = x0;
	self->last_y0 = y0;
	self->rendered = true;

	wlr_scene_buffer_set_buffer(self->scene_buffer, &buffer->base);
	wlr_scene_buffer_set_dest_size(self->scene_buffer,
		self->total_width, self->total_height);
}

static void
_handle_outputs_update(struct wl_listener *listener, void *data)
{
	struct scaled_corner_buffer *self =
		wl_container_of(listener, self, outputs_update);
	struct wlr_scene_outputs_update_event *event = data;
	double max_scale = 0;

	for (size_t i = 0; i < event->size; i++) {
		max_scale = MAX(max_scale, event->active[i]->output->scale);
	}
	if (max_scale && self->active_scale != max_scale) {
		self->active_scale = max_scale;
		self->rendered = false;
		_render(self);
	}
}

static void
_handle_node_destroy(struct wl_listener *listener, void *data)
{
	struct scaled_corner_buffer *self =
		wl_container_of(listener, self, destroy);

	wl_list_remove(&self->outputs_update.link);
	wl_list_remove(&self->destroy.link);
	free(self);
}

/* Public API */
struct scaled_corner_buffer *
scaled_corner_buffer_create(struct wlr_scene_tree *parent)
{
	assert(parent);
	struct scaled_corner_buffer *self = znew(*self);

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
scaled_corner_buffer_update(struct scaled_corner_buffer *self,
		int width, int height, int radius, int border_width,
		cairo_pattern_t *fill_pattern, const float *border_color, bool right,
		double node_x, double node_y, double edge_x, double edge_y)
{
	assert(self);
	assert(fill_pattern);
	assert(border_color);

	int total_width = round_up_4(width + 4);
	int total_height = round_up_4(height + 4);

	if (self->width == width && self->height == height
			&& self->radius == radius && self->border_width == border_width
			&& self->fill_pattern == fill_pattern
			&& self->right == right
			&& self->node_x == node_x && self->node_y == node_y
			&& self->edge_x == edge_x && self->edge_y == edge_y
			&& !memcmp(self->border_color, border_color, sizeof(self->border_color))) {
		return;
	}

	self->width = width;
	self->height = height;
	self->total_width = total_width;
	self->total_height = total_height;
	self->radius = radius;
	self->border_width = border_width;
	self->fill_pattern = fill_pattern;
	self->right = right;
	self->node_x = node_x;
	self->node_y = node_y;
	self->edge_x = edge_x;
	self->edge_y = edge_y;
	memcpy(self->border_color, border_color, sizeof(self->border_color));

	if (self->active_scale <= 0) {
		self->active_scale = 1;
	}
	_render(self);
}
