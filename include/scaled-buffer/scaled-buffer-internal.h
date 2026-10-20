/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_BUFFER_INTERNAL_H
#define LABWC_SCALED_BUFFER_INTERNAL_H

/*
 * Last scale seen on any output; used to bootstrap new or not yet visible
 * scaled drawing buffers so they do not flash at scale 1 when first shown.
 * One definition lives in scaled-rect-buffer.c and is shared by all scaled
 * drawing primitives so that sibling rect/corner buffers cannot bootstrap
 * at different scales.
 */
extern double scaled_buffer_last_output_scale;

#endif /* LABWC_SCALED_BUFFER_INTERNAL_H */
