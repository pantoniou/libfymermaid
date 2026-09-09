/*
 * fymm-term.c - what the terminal can show, and what it looks like
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#define FYMM_HAVE_TTY 1
#endif

#include "fymm-internal.h"

/* how long to wait for a terminal to answer a query, in milliseconds */
#define FYMM_QUERY_TIMEOUT_MS 100

/*
 * The terminals that do 24 bit colour but whose TERM does not say so.
 * kitty is `xterm-kitty`, ghostty `xterm-ghostty`, and neither contains
 * `256color` or `-direct`; read as TERM alone they would fall back to the
 * sixteen ANSI colours.
 *
 * Each entry is matched as a prefix of TERM, so `foot-extra` matches `foot`.
 */
static const char *const fymm_truecolor_terms[] = {
	"xterm-kitty", "xterm-ghostty", "ghostty", "wezterm", "alacritty",
	"foot", "contour", "rio", "vte", "konsole", "iterm", "kitty",
};

/*
 * The environment a terminal emulator sets for itself. A terminal multiplexer
 * often drops COLORTERM, so its own marker is what is left to go on.
 */
static const char *const fymm_truecolor_env[] = {
	"KITTY_WINDOW_ID", "GHOSTTY_RESOURCES_DIR", "GHOSTTY_BIN_DIR",
	"WEZTERM_EXECUTABLE", "WEZTERM_PANE", "ALACRITTY_WINDOW_ID",
	"ALACRITTY_SOCKET", "KONSOLE_VERSION", "ITERM_SESSION_ID",
	"WT_SESSION", "VSCODE_INJECTION",
};

/* TERM_PROGRAM values that name a 24 bit terminal. */
static const char *const fymm_truecolor_programs[] = {
	"iTerm.app", "WezTerm", "ghostty", "vscode", "Hyper", "Tabby",
	"rio", "warp", "WarpTerminal",
};

static bool fymm_env_set(const char *const *names, size_t count)
{
	const char *v;
	size_t i;

	for (i = 0; i < count; i++) {
		v = getenv(names[i]);
		if (v && *v)
			return true;
	}
	return false;
}

static bool fymm_str_in(const char *value, const char *const *list,
			size_t count, bool prefix)
{
	size_t i, l;

	if (!value || !*value)
		return false;
	for (i = 0; i < count; i++) {
		l = strlen(list[i]);
		if (prefix ? !strncasecmp(value, list[i], l) :
			     !strcasecmp(value, list[i]))
			return true;
	}
	return false;
}

int fymm_detect_width(int fd)
{
	const char *env = getenv("COLUMNS");

	if (env && *env) {
		int w = atoi(env);

		if (w > 0)
			return w;
	}
#ifdef FYMM_HAVE_TTY
	{
		struct winsize ws;

		if (fd >= 0 && !ioctl(fd, TIOCGWINSZ, &ws) && ws.ws_col > 0)
			return ws.ws_col;
	}
#else
	(void)fd;
#endif
	return 80;
}

enum fymm_color_mode fymm_detect_color_mode(int fd)
{
	const char *env;

	/* https://no-color.org: any non-empty value disables colour */
	env = getenv("NO_COLOR");
	if (env && *env)
		return FYMM_COLOR_NONE;

	env = getenv("CLICOLOR_FORCE");
	if (!(env && *env && strcmp(env, "0")) && fd >= 0 && !isatty(fd))
		return FYMM_COLOR_NONE;

	/* the de facto marker a 24 bit terminal sets for itself */
	env = getenv("COLORTERM");
	if (env && (!strcmp(env, "truecolor") || !strcmp(env, "24bit")))
		return FYMM_COLOR_TRUECOLOR;

	env = getenv("TERM");
	if (env && (!*env || !strcmp(env, "dumb")))
		return FYMM_COLOR_NONE;

	/*
	 * A terminfo entry ending in `-direct` is the direct-colour form of
	 * its terminal: `xterm-direct` and `tmux-direct` mean 24 bit.
	 */
	if (env && (strstr(env, "-direct") || strstr(env, "truecolor")))
		return FYMM_COLOR_TRUECOLOR;

	/* the terminals whose own name is the only clue */
	if (fymm_str_in(env, fymm_truecolor_terms,
			sizeof(fymm_truecolor_terms) /
			sizeof(fymm_truecolor_terms[0]), true))
		return FYMM_COLOR_TRUECOLOR;
	if (fymm_str_in(getenv("TERM_PROGRAM"), fymm_truecolor_programs,
			sizeof(fymm_truecolor_programs) /
			sizeof(fymm_truecolor_programs[0]), false))
		return FYMM_COLOR_TRUECOLOR;
	if (fymm_env_set(fymm_truecolor_env,
			 sizeof(fymm_truecolor_env) /
			 sizeof(fymm_truecolor_env[0])))
		return FYMM_COLOR_TRUECOLOR;

	if (!env || !*env)
		return FYMM_COLOR_NONE;
	if (strstr(env, "256color") || strstr(env, "256"))
		return FYMM_COLOR_256;
	return FYMM_COLOR_16;
}

enum fymm_charset fymm_detect_charset(void)
{
	static const char *const vars[] = { "LC_ALL", "LC_CTYPE", "LANG" };
	size_t i;

	for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
		const char *env = getenv(vars[i]);

		if (!env || !*env)
			continue;
		return strstr(env, "UTF-8") || strstr(env, "utf8") ||
		       strstr(env, "UTF8") || strstr(env, "utf-8") ?
		       FYMM_CHARSET_UNICODE : FYMM_CHARSET_ASCII;
	}
	return FYMM_CHARSET_ASCII;
}

/*
 * `COLORFGBG` is `<foreground>;<background>`, sometimes with a third field
 * between them. The last field is the background as an ANSI index, and the
 * light half of the sixteen is 7 and 9 upwards.
 */
static enum fymm_background fymm_background_from_colorfgbg(void)
{
	const char *env = getenv("COLORFGBG");
	const char *last;
	char *end;
	long bg;

	if (!env || !*env)
		return FYMM_BG_AUTO;

	last = strrchr(env, ';');
	last = last ? last + 1 : env;
	if (!strcasecmp(last, "default"))
		return FYMM_BG_DARK;

	end = NULL;
	bg = strtol(last, &end, 10);
	if (!end || end == last || *end)
		return FYMM_BG_AUTO;
	return (bg == 7 || bg >= 9) ? FYMM_BG_LIGHT : FYMM_BG_DARK;
}

#ifdef FYMM_HAVE_TTY

/* Read one hex component of an OSC 11 answer, scaled to eight bits. */
static int fymm_osc_component(const char *s, const char *e)
{
	unsigned long v = 0;
	int digits = 0;

	for (; s < e && isxdigit((unsigned char)*s); s++, digits++) {
		v <<= 4;
		v |= (unsigned long)(isdigit((unsigned char)*s) ? *s - '0' :
				     (tolower((unsigned char)*s) - 'a' + 10));
	}
	if (!digits)
		return -1;
	/* the answer may be 1 to 4 digits per component */
	while (digits < 4) {
		v = (v << 4) | (v & 0xf);
		digits++;
	}
	return (int)(v >> 8);
}

/*
 * Ask the terminal what its background is, with OSC 11. The answer looks like
 * `ESC ] 11 ; rgb:RRRR/GGGG/BBBB` and ends in BEL or ST.
 *
 * A terminal that does not answer must not hang the render, so the read is
 * polled with a short timeout and the terminal is put back exactly as it was
 * whatever happens. The query goes to /dev/tty rather than to stdout, so a
 * redirected render never has escape bytes injected into it.
 */
static enum fymm_background fymm_background_from_query(int fd)
{
	static const char query[] = "\033]11;?\033\\";
	struct termios saved, raw;
	char buf[128];
	const char *p, *e;
	struct pollfd pfd;
	size_t used = 0;
	ssize_t n;
	int tty, rgb[3], i, flags;
	bool own_tty = true;
	enum fymm_background out = FYMM_BG_AUTO;

	/*
	 * The controlling terminal, so that a render being redirected never
	 * has the query written into it. Where there is no controlling
	 * terminal but the caller is drawing to one, ask that instead.
	 *
	 * The open must not sleep for carrier: a terminal opened for
	 * reading without O_NONBLOCK waits for carrier on the BSDs, and a
	 * render must not hang on the open. The flag is cleared below, so
	 * the query runs on a blocking terminal as before.
	 */
	tty = open("/dev/tty", O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (tty >= 0) {
		flags = fcntl(tty, F_GETFL);
		if (flags >= 0)
			fcntl(tty, F_SETFL, flags & ~O_NONBLOCK);
	}
	if (tty < 0) {
		if (fd < 0 || !isatty(fd))
			return FYMM_BG_AUTO;
		tty = fd;
		own_tty = false;
	}
	if (!isatty(tty) || tcgetattr(tty, &saved))
		goto out_close;

	raw = saved;
	raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(tty, TCSANOW, &raw))
		goto out_close;

	if (write(tty, query, sizeof(query) - 1) != (ssize_t)(sizeof(query) - 1))
		goto out_restore;

	/* read until the answer terminates, or until the terminal has had
	 * long enough to have answered */
	for (;;) {
		pfd.fd = tty;
		pfd.events = POLLIN;
		n = poll(&pfd, 1, FYMM_QUERY_TIMEOUT_MS);
		if (n <= 0)
			break;
		n = read(tty, buf + used, sizeof(buf) - 1 - used);
		if (n <= 0)
			break;
		used += (size_t)n;
		buf[used] = '\0';
		if (memchr(buf, '\a', used) || strstr(buf, "\033\\"))
			break;
		if (used + 1 >= sizeof(buf))
			break;
	}

	if (!used)
		goto out_restore;

	p = strstr(buf, "rgb:");
	if (!p)
		goto out_restore;
	p += 4;
	e = buf + used;
	for (i = 0; i < 3; i++) {
		const char *slash = memchr(p, '/', (size_t)(e - p));
		const char *stop = slash ? slash : e;

		rgb[i] = fymm_osc_component(p, stop);
		if (rgb[i] < 0)
			goto out_restore;
		if (!slash)
			break;
		p = slash + 1;
	}
	if (i < 2)
		goto out_restore;

	/*
	 * Rec. 601 luma, which is what a reader's eye weights the channels
	 * by; halfway is the boundary between a light and a dark terminal.
	 */
	out = (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000 >= 128 ?
	      FYMM_BG_LIGHT : FYMM_BG_DARK;

out_restore:
	tcsetattr(tty, TCSANOW, &saved);
out_close:
	if (own_tty)
		close(tty);
	return out;
}

#else

static enum fymm_background fymm_background_from_query(int fd)
{
	(void)fd;
	return FYMM_BG_AUTO;
}

#endif

enum fymm_background fymm_detect_background(int fd)
{
	const char *env = getenv("FYMM_BACKGROUND");
	enum fymm_background bg;

	/* an explicit answer settles it */
	if (env && *env) {
		if (!strcasecmp(env, "light"))
			return FYMM_BG_LIGHT;
		if (!strcasecmp(env, "dark"))
			return FYMM_BG_DARK;
	}

	bg = fymm_background_from_colorfgbg();
	if (bg != FYMM_BG_AUTO)
		return bg;

	/* asking costs a round trip, so only ask a terminal we are drawing
	 * to, and only when it can answer */
	if (fd < 0 || !isatty(fd))
		return FYMM_BG_DARK;

	bg = fymm_background_from_query(fd);
	return bg != FYMM_BG_AUTO ? bg : FYMM_BG_DARK;
}
