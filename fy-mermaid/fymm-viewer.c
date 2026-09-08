/*
 * fymm-viewer.c - move a selection over a rendered diagram
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include "fymm-viewer.h"

/* the alternate screen, the cursor and the SGR mouse reports */
#define VW_ENTER "\033[?1049h\033[?25l\033[?1000h\033[?1006h"
#define VW_LEAVE "\033[?1006l\033[?1000l\033[?25h\033[?1049l"

/* A resize is taken on the next key, so the loop never draws from a signal. */
static volatile sig_atomic_t vw_resized;

static void vw_winch(int sig)
{
	(void)sig;
	vw_resized = 1;
}

static void vw_size(int *wp, int *hp)
{
	struct winsize ws;

	*wp = 80;
	*hp = 24;
	if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) && ws.ws_col && ws.ws_row) {
		*wp = ws.ws_col;
		*hp = ws.ws_row;
	}
}

/*
 * Draw the diagram and, under it, what the selection is. The status line is
 * the only place the path is shown, so a consumer can see what it would get
 * from fymm_hit_test().
 */
static void vw_draw(const struct fymm_render_result *r, int h)
{
	const struct fymm_element *e;
	const char *path;

	fputs("\033[H\033[2J", stdout);
	fputs(fymm_render_result_text(r), stdout);

	path = fymm_render_result_selection(r);
	e = fymm_render_result_find(r, path);
	printf("\033[%d;1H\033[7m", h);
	if (e)
		printf(" %s  %d,%d %dx%d ", e->path, e->row, e->col, e->width,
		       e->height);
	else
		printf(" nothing selected ");
	printf("\033[0m  arrows move, tab cycles, click selects, q quits");
	fflush(stdout);
}

/*
 * One key, or one mouse report. A key is returned as itself, an arrow as one
 * of the negative codes, and a click sets @rowp and @colp and returns
 * VW_CLICK. It returns 0 when the input ends.
 */
#define VW_UP    (-1)
#define VW_DOWN  (-2)
#define VW_RIGHT (-3)
#define VW_LEFT  (-4)
#define VW_CLICK (-5)
#define VW_BACKTAB (-6)

static int vw_key(int *rowp, int *colp)
{
	char seq[32];
	size_t n = 0;
	int b, x, y;
	char c;

	if (read(STDIN_FILENO, &c, 1) != 1)
		return 0;
	if (c != '\033')
		return (unsigned char)c;

	if (read(STDIN_FILENO, &c, 1) != 1)
		return 'q';
	if (c != '[')
		return (unsigned char)c;

	while (n + 1 < sizeof(seq)) {
		if (read(STDIN_FILENO, &c, 1) != 1)
			return 0;
		seq[n++] = c;
		if ((c >= '@' && c <= '~') && c != '<')
			break;
	}
	seq[n] = '\0';

	switch (seq[0]) {
	case 'A':
		return VW_UP;
	case 'B':
		return VW_DOWN;
	case 'C':
		return VW_RIGHT;
	case 'D':
		return VW_LEFT;
	case 'Z':
		return VW_BACKTAB;
	default:
		break;
	}

	/* an SGR mouse report: <button;col;rowM for a press */
	if (seq[0] == '<' && sscanf(seq + 1, "%d;%d;%d", &b, &x, &y) == 3 &&
	    seq[n - 1] == 'M' && !(b & 0x60)) {
		*colp = x - 1;
		*rowp = y - 1;
		return VW_CLICK;
	}
	return -100;
}

static void vw_move(struct fymm_render_result *r, enum fymm_direction dir)
{
	const struct fymm_element *e;

	e = fymm_navigate(r, fymm_render_result_selection(r), dir);
	if (e)
		fymm_render_result_select(r, e->path, FYMM_SEL_AUTO);
}

int fymm_viewer_run(struct fymm_diagram *d, const struct fymm_render_cfg *cfg,
		    const char *progname)
{
	struct fymm_metrics met = FYMM_METRICS_INIT;
	struct sigaction sa, osa;
	struct termios tio, otio;
	struct fymm_render_result *r;
	const struct fymm_element *e;
	struct fymm_render_cfg lcfg;
	char *keep = NULL;
	int w, h, key, row = 0, col = 0;
	bool done = false;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		fprintf(stderr, "%s: --interactive needs a terminal\n",
			progname);
		return -1;
	}
	if (tcgetattr(STDIN_FILENO, &otio)) {
		fprintf(stderr, "%s: cannot read the terminal state\n",
			progname);
		return -1;
	}

	lcfg = *cfg;
	if (cfg->metrics)
		met = *cfg->metrics;
	lcfg.metrics = &met;
	lcfg.selection_style = FYMM_SEL_AUTO;

	tio = otio;
	tio.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = vw_winch;
	sigaction(SIGWINCH, &sa, &osa);

	fputs(VW_ENTER, stdout);

	vw_size(&w, &h);
	lcfg.width = w;
	met.max_height = h - 1;
	r = fymm_render_ex(d, &lcfg);
	if (r)
		vw_move(r, FYMM_DIR_NEXT);

	while (r && !done) {
		vw_draw(r, h);

		key = vw_key(&row, &col);
		if (vw_resized) {
			struct fymm_render_result *nr;

			vw_resized = 0;
			vw_size(&w, &h);
			lcfg.width = w;
			met.max_height = h - 1;
			lcfg.selection = fymm_render_result_selection(r);
			nr = fymm_render_ex(d, &lcfg);
			if (nr) {
				fymm_render_result_destroy(r);
				r = nr;
			}
			lcfg.selection = NULL;
		}

		switch (key) {
		case 0:
		case 'q':
		case 'Q':
		case 3:		/* ^C */
			done = true;
			break;
		case VW_UP:
		case 'k':
			vw_move(r, FYMM_DIR_UP);
			break;
		case VW_DOWN:
		case 'j':
			vw_move(r, FYMM_DIR_DOWN);
			break;
		case VW_LEFT:
		case 'h':
			vw_move(r, FYMM_DIR_LEFT);
			break;
		case VW_RIGHT:
		case 'l':
			vw_move(r, FYMM_DIR_RIGHT);
			break;
		case '\t':
			vw_move(r, FYMM_DIR_NEXT);
			break;
		case VW_BACKTAB:
			vw_move(r, FYMM_DIR_PREV);
			break;
		case VW_CLICK:
			e = fymm_hit_test(r, row, col);
			if (e)
				fymm_render_result_select(r, e->path,
							  FYMM_SEL_AUTO);
			break;
		case '\r':
		case '\n':
			/* the path is reported once the screen is back */
			if (fymm_render_result_selection(r))
				keep = strdup(fymm_render_result_selection(r));
			done = true;
			break;
		default:
			break;
		}
	}

	fputs(VW_LEAVE, stdout);
	fflush(stdout);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &otio);
	sigaction(SIGWINCH, &osa, NULL);

	if (keep) {
		printf("%s\n", keep);
		free(keep);
	}
	if (!r) {
		fprintf(stderr, "%s: cannot render the diagram\n", progname);
		return -1;
	}
	fymm_render_result_destroy(r);
	return 0;
}
