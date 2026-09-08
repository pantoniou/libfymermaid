/*
 * fymm-viewer.h - the interactive viewer of fy-mermaid
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef FYMM_VIEWER_H
#define FYMM_VIEWER_H

#include <libfymermaid.h>

/*
 * Show @d and let the user move a selection over it with the arrow keys and
 * with the mouse. It needs a terminal on both standard input and standard
 * output; without one it reports the reason and does nothing.
 *
 * Returns: 0 when the viewer ran, -1 when it could not.
 */
int fymm_viewer_run(struct fymm_diagram *d, const struct fymm_render_cfg *cfg,
		    const char *progname);

#endif /* FYMM_VIEWER_H */
