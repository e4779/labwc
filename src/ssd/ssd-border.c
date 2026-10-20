// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "common/macros.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "scaled-buffer/scaled-rect-buffer.h"
#include "scaled-buffer/scaled-rect-buffer.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

void
ssd_border_create(struct ssd *ssd)
{
	assert(ssd);
	assert(!ssd->border.tree);

	struct view *view = ssd->view;
	struct theme *theme = rc.theme;
	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_width = width + 2 * theme->border_width;
	int corner_width = ssd_get_corner_width();

	ssd->border.tree = lab_wlr_scene_tree_create(ssd->tree);
	wlr_scene_node_set_position(&ssd->border.tree->node, -theme->border_width, 0);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];
		subtree->tree = lab_wlr_scene_tree_create(ssd->border.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);
		float *color = theme->window[active].border_color;

		int vx = view->current.x;
		int vy = view->current.y;

		subtree->left = scaled_rect_buffer_create(parent);
		wlr_scene_node_set_position(&subtree->left->scene_buffer->node, 0, 0);
		scaled_rect_buffer_update(subtree->left,
			theme->border_width, height, color,
			SCALED_RECT_HUG_END, SCALED_RECT_SPAN_SNAP,
			vx - theme->border_width, vy, vx, 0, 0, 0);

		subtree->right = scaled_rect_buffer_create(parent);
		wlr_scene_node_set_position(&subtree->right->scene_buffer->node,
			theme->border_width + width, 0);
		scaled_rect_buffer_update(subtree->right,
			theme->border_width, height, color,
			SCALED_RECT_HUG_START, SCALED_RECT_SPAN_SNAP,
			vx + width, vy, vx + width, 0, 0, 0);

		subtree->bottom = scaled_rect_buffer_create(parent);
		wlr_scene_node_set_position(&subtree->bottom->scene_buffer->node,
			0, height);
		scaled_rect_buffer_update(subtree->bottom,
			width, theme->border_width, color,
			SCALED_RECT_SPAN_BETWEEN, SCALED_RECT_HUG_START,
			vx - theme->border_width, vy + height, vx, vy + height,
			0, 0);

		/* Titlebar-colored underlay hugging the titlebar top edge
		 * from below the border ink. When the window phase rounds
		 * the titlebar fill one device row below the border ink, the
		 * underlay fills that hole; otherwise it is hidden below the
		 * ink strip (same rows, opaque ink on top). */
		int tw = (theme->border_width + 7) & ~3;
		subtree->underlay = scaled_rect_buffer_create(parent);
		wlr_scene_node_set_position(
			&subtree->underlay->scene_buffer->node,
			0, -(ssd->titlebar.height + tw));
		scaled_rect_buffer_update(subtree->underlay,
			width, theme->border_width,
			theme->window[active].title_bg.color,
			SCALED_RECT_SPAN_BETWEEN, SCALED_RECT_HUG_END,
			vx, vy - ssd->titlebar.height - tw,
			vx, vy - ssd->titlebar.height,
			ssd_should_be_squared(ssd) ? 0 : rc.corner_radius,
			ssd_should_be_squared(ssd) ? 0 : rc.corner_radius);

		subtree->top = scaled_rect_buffer_create(parent);
		wlr_scene_node_set_position(&subtree->top->scene_buffer->node,
			0, -(ssd->titlebar.height + theme->border_width));
		scaled_rect_buffer_update(subtree->top,
			width, theme->border_width, color,
			SCALED_RECT_SPAN_BETWEEN, SCALED_RECT_HUG_START,
			vx - theme->border_width,
			vy - ssd->titlebar.height - theme->border_width,
			vx, vy - ssd->titlebar.height - theme->border_width,
			ssd_should_be_squared(ssd) ? 0 : rc.corner_radius,
			ssd_should_be_squared(ssd) ? 0 : rc.corner_radius);
	}

	if (view->maximized == VIEW_AXIS_BOTH) {
		wlr_scene_node_set_enabled(&ssd->border.tree->node, false);
	}

	if (view->current.width > 0 && view->current.height > 0) {
		/*
		 * The SSD is recreated by a Reconfigure request
		 * thus we may need to handle squared corners.
		 */
		ssd_border_update(ssd);
	}
}

void
ssd_border_update(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	struct view *view = ssd->view;
	if (view->maximized == VIEW_AXIS_BOTH
			&& ssd->border.tree->node.enabled) {
		/* Disable borders on maximize */
		wlr_scene_node_set_enabled(&ssd->border.tree->node, false);
		ssd->margin = ssd_thickness(ssd->view);
	}

	if (view->maximized == VIEW_AXIS_BOTH) {
		return;
	} else if (!ssd->border.tree->node.enabled) {
		/* And re-enabled them when unmaximized */
		wlr_scene_node_set_enabled(&ssd->border.tree->node, true);
		ssd->margin = ssd_thickness(ssd->view);
	}

	struct theme *theme = rc.theme;

	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_width = width + 2 * theme->border_width;
	int corner_width = ssd_get_corner_width();

	/*
	 * From here on we have to cover the following border scenarios:
	 * Non-tiled (partial border, rounded corners):
	 *    _____________
	 *   o           oox
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled (full border, squared corners):
	 *   _______________
	 *  |o           oox|
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled or non-tiled with zero title height (full boarder, no title):
	 *   _______________
	 *  |_______________|
	 */

	int side_height = ssd->state.was_squared
		? height + ssd->titlebar.height
		: height;
	int side_y = ssd->state.was_squared
		? -ssd->titlebar.height
		: 0;
	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];

		int vx = view->current.x;
		int vy = view->current.y;
		float *color = theme->window[active].border_color;

		scaled_rect_buffer_update(subtree->left,
			theme->border_width, side_height, color,
			SCALED_RECT_HUG_END, SCALED_RECT_SPAN_SNAP,
			vx - theme->border_width, vy + side_y, vx, 0, 0, 0);
		wlr_scene_node_set_position(&subtree->left->scene_buffer->node,
			0, side_y);

		scaled_rect_buffer_update(subtree->right,
			theme->border_width, side_height, color,
			SCALED_RECT_HUG_START, SCALED_RECT_SPAN_SNAP,
			vx + width, vy + side_y, vx + width, 0, 0, 0);
		wlr_scene_node_set_position(&subtree->right->scene_buffer->node,
			theme->border_width + width, side_y);

		scaled_rect_buffer_update(subtree->bottom,
			width, theme->border_width, color,
			SCALED_RECT_SPAN_BETWEEN, SCALED_RECT_HUG_START,
			vx - theme->border_width, vy + height, vx, vy + height,
			0, 0);
		wlr_scene_node_set_position(&subtree->bottom->scene_buffer->node,
			0, height);

		{
			int tw = (theme->border_width + 7) & ~3;
			scaled_rect_buffer_update(subtree->underlay,
				width, theme->border_width,
				theme->window[active].title_bg.color,
				SCALED_RECT_SPAN_BETWEEN, SCALED_RECT_HUG_END,
				vx, vy - ssd->titlebar.height - tw,
				vx, vy - ssd->titlebar.height,
				ssd_should_be_squared(ssd) ? 0 : rc.corner_radius,
				ssd_should_be_squared(ssd) ? 0 : rc.corner_radius);
			wlr_scene_node_set_position(
				&subtree->underlay->scene_buffer->node,
				0, -(ssd->titlebar.height + tw));
		}

		scaled_rect_buffer_update(subtree->top,
			width, theme->border_width, color,
			SCALED_RECT_SPAN_BETWEEN, SCALED_RECT_HUG_START,
			vx - theme->border_width,
			vy - ssd->titlebar.height - theme->border_width,
			vx, vy - ssd->titlebar.height - theme->border_width,
			ssd_should_be_squared(ssd) ? 0 : rc.corner_radius,
			ssd_should_be_squared(ssd) ? 0 : rc.corner_radius);
		wlr_scene_node_set_position(&subtree->top->scene_buffer->node,
			0, -(ssd->titlebar.height + theme->border_width));
	}
}

void
ssd_border_destroy(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	wlr_scene_node_destroy(&ssd->border.tree->node);
	ssd->border = (struct ssd_border_scene){0};
}
