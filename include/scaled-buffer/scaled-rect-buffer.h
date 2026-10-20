/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_RECT_BUFFER_H
#define LABWC_SCALED_RECT_BUFFER_H

#include <wayland-server-core.h>
#include <stdbool.h>

struct wlr_scene_tree;

/*
 * How the visible rectangle is placed on one axis of the buffer.
 *
 * wlroots snaps every scene graph box edge independently via
 * round(logical * scale) (see scale_box() in wlroots). The SPAN modes
 * fill along the axis; the HUG modes place a rectangle of
 * round(length * scale) device pixels flush against the snapped
 * position of a given logical edge coordinate, so that e.g. a window
 * border lands exactly on the device pixel where the (snapped)
 * content edge of the window begins, with the fractional remainder
 * falling onto the other (usually invisible) side.
 */
enum scaled_rect_axis {
	SCALED_RECT_SPAN,      /* fill the whole axis of the buffer */
	SCALED_RECT_SPAN_SNAP, /* fill from 0 to the snapped logical length */
	SCALED_RECT_HUG_START, /* round(length*scale) px starting at snapped edge */
	SCALED_RECT_HUG_END,   /* round(length*scale) px ending at snapped edge */
	SCALED_RECT_SPAN_BETWEEN, /* from snapped(edge)-t to snapped(edge+width)+t */
};

/*
 * A solid color rectangle rendered into a buffer at the scale of the
 * output it is displayed on, aligned to device pixel grid positions
 * derived from the same rounding wlroots applies when rendering scene
 * graph boxes. This gives the rectangle a constant, position
 * independent appearance at fractional output scales.
 */
struct scaled_rect_buffer {
	struct wlr_scene_buffer *scene_buffer;
	int width;        /* visible rectangle, logical, read only */
	int height;       /* visible rectangle, logical, read only */
	int total_width;  /* buffer dest width incl. padding, logical, read only */
	int total_height; /* buffer dest height incl. padding, logical, read only */

	/* Private */
	float color[4];
	enum scaled_rect_axis mode_x;
	enum scaled_rect_axis mode_y;
	double node_x; /* absolute logical position of the node */
	double node_y;
	double edge_x; /* absolute logical edge to hug (HUG modes) */
	double edge_y;
	int inset_start; /* SPAN_BETWEEN insets (logical) */
	int inset_end;
	double active_scale;
	int last_ink_x, last_ink_y, last_ink_w, last_ink_h;
	float last_color[4];
	struct wl_listener outputs_update;
	struct wl_listener destroy;
};

struct scaled_rect_buffer *scaled_rect_buffer_create(struct wlr_scene_tree *parent);

/**
 * Update geometry and color and re-render the backing buffer if the
 * resulting device pixel placement changed. node_x/node_y are the
 * absolute logical coordinates of the scene buffer node; edge_x/edge_y
 * the absolute logical coordinate of the edge to hug (only used by the
 * HUG modes on their respective axis).
 */
void scaled_rect_buffer_update(struct scaled_rect_buffer *self,
	int width, int height, const float *color,
	enum scaled_rect_axis mode_x, enum scaled_rect_axis mode_y,
	double node_x, double node_y, double edge_x, double edge_y,
	int inset_start, int inset_end);

/** Change only the color and re-render if needed. */
void scaled_rect_buffer_set_color(struct scaled_rect_buffer *self,
	const float *color);

#endif /* LABWC_SCALED_RECT_BUFFER_H */
