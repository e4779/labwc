/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_RECT_BUFFER_H
#define LABWC_SCALED_RECT_BUFFER_H

struct wlr_scene_tree;
struct wlr_scene_buffer;
struct scaled_buffer;

/*
 * A solid color rectangle rendered into a buffer at the scale of the
 * output it is displayed on.
 *
 * In contrast to a plain wlr_scene_rect node, the backing buffer is
 * rendered in device pixels which ensures the rectangle has a
 * consistent (device pixel aligned) width for all window positions
 * when using fractional output scales.
 */
struct scaled_rect_buffer {
	struct wlr_scene_buffer *scene_buffer;
	int width;   /* unscaled, read only */
	int height;  /* unscaled, read only */

	/* Private */
	float color[4];
	struct scaled_buffer *scaled_buffer;
};

/**
 * Create an auto scaling solid color rectangle buffer, providing a
 * wlr_scene_buffer node for display. It gets destroyed automatically
 * when the backing scene buffer (or one of its parents) is destroyed.
 *
 * Initially the rectangle has zero size and is thus invisible until
 * scaled_rect_buffer_update() is called.
 */
struct scaled_rect_buffer *scaled_rect_buffer_create(struct wlr_scene_tree *parent);

/**
 * Update size and color and request re-rendering the backing buffer.
 * It is safe to call this repeatedly with unchanged arguments.
 */
void scaled_rect_buffer_update(struct scaled_rect_buffer *self,
	int width, int height, const float *color);

#endif /* LABWC_SCALED_RECT_BUFFER_H */
