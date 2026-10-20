/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_CORNER_BUFFER_H
#define LABWC_SCALED_CORNER_BUFFER_H

#include <wayland-server-core.h>
#include <stdbool.h>

#include <cairo.h>

struct wlr_scene_tree;

/*
 * A titlebar corner piece (rounded or squared) rendered into a buffer
 * at the scale of the output it is displayed on, aligned to the same
 * device pixel anchors that scaled_rect_buffer uses for the straight
 * window borders (see scaled-rect-buffer.h).
 *
 * The drawing replicates theme.c rounded_rect() but is rigidly shifted
 * by whole device pixels so that the straight border segments of the
 * corner land exactly on the rows/columns of the straight borders,
 * removing the titlebar-vs-client snap disagreement at fractional
 * scales.
 */
struct scaled_corner_buffer {
	struct wlr_scene_buffer *scene_buffer;
	int width;        /* corner box width, logical, read only */
	int height;       /* corner box height, logical, read only */
	int total_width;  /* buffer dest width incl. padding, logical */
	int total_height; /* buffer dest height incl. padding, logical */

	/* Private */
	int radius;
	int border_width;
	cairo_pattern_t *fill_pattern;
	float border_color[4];
	bool right; /* false: top-left corner, true: top-right corner */
	double node_x, node_y; /* absolute logical node position */
	double edge_x; /* content edge to align the side segment with */
	double edge_y; /* titlebar top edge to align the top segment with */
	double active_scale;
	int last_x0, last_y0; /* last rendered device anchor */
	bool rendered;
	struct wl_listener outputs_update;
	struct wl_listener destroy;
};

struct scaled_corner_buffer *scaled_corner_buffer_create(struct wlr_scene_tree *parent);

/**
 * Update geometry, colors and anchors and re-render if the resulting
 * device pixel placement changed. Anchors are absolute logical
 * coordinates (see scaled_rect_buffer_update()).
 */
void scaled_corner_buffer_update(struct scaled_corner_buffer *self,
	int width, int height, int radius, int border_width,
	cairo_pattern_t *fill_pattern, const float *border_color, bool right,
	double node_x, double node_y, double edge_x, double edge_y);

#endif /* LABWC_SCALED_CORNER_BUFFER_H */
