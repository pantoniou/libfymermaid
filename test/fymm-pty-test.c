/*
 * fymm-pty-test.c - the terminal background query, against a real terminal
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

/*
 * fymm_detect_background() asks the terminal what it is drawn on with an
 * OSC 11 query. That needs a terminal to answer, so this test makes one: a
 * pty whose slave is the child's controlling terminal, with the parent
 * playing the terminal at the other end.
 *
 * What has to hold: the answer is read and believed, and a terminal that
 * never answers times out rather than hanging the caller.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
/* kill() the child when it wedges instead of waiting on it forever */
#include <signal.h>
/* openpty(3) lives in <util.h> on the BSDs, <pty.h> on glibc */
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* <util.h> does not declare ioctl() the way <pty.h> does */
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <libfymermaid.h>

/* longer than the library's own query timeout, so a hang is visible */
#define PTY_WAIT_MS 3000

static int failures;

#define CHECK(_cond, _fmt, ...)                                              \
	do {                                                                 \
		if (!(_cond)) {                                              \
			fprintf(stderr, "%s:%d: " _fmt "\n", __func__,       \
				__LINE__, ##__VA_ARGS__);                    \
			failures++;                                          \
		}                                                            \
	} while (0)

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/*
 * Run fymm_detect_background() in a child whose controlling terminal is a
 * pty, answering its query with @answer (NULL to stay silent). Returns what
 * the child detected, and how long it took through @elapsed.
 */
static enum fymm_background probe(const char *answer, double *elapsed)
{
	char buf[256];
	struct pollfd pfd;
	int master, slave, status;
	double start;
	ssize_t n;
	pid_t pid;

	if (openpty(&master, &slave, NULL, NULL, NULL)) {
		fprintf(stderr, "openpty: %s\n", strerror(errno));
		return FYMM_BG_AUTO;
	}

	start = now_ms();
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork: %s\n", strerror(errno));
		return FYMM_BG_AUTO;
	}

	if (!pid) {
		enum fymm_background bg;

		/* the child's own session, with the pty as its terminal */
		close(master);
		setsid();
		ioctl(slave, TIOCSCTTY, 0);
		dup2(slave, STDIN_FILENO);
		dup2(slave, STDOUT_FILENO);

		unsetenv("COLORFGBG");
		unsetenv("FYMM_BACKGROUND");
		bg = fymm_detect_background(STDOUT_FILENO);
		_exit((int)bg);
	}

	close(slave);

	/* play the terminal: wait for the query, then answer it */
	pfd.fd = master;
	pfd.events = POLLIN;
	while (poll(&pfd, 1, PTY_WAIT_MS) > 0) {
		n = read(master, buf, sizeof(buf) - 1);
		if (n <= 0)
			break;
		buf[n] = '\0';
		if (strstr(buf, "\033]11;?")) {
			if (answer)
				(void)!write(master, answer, strlen(answer));
			break;
		}
	}

	/*
	 * The child must be done by now: the query carries a 100ms timeout,
	 * so reap it with a deadline and fail loud if it wedged. An
	 * unbounded waitpid() here turned macOS CI into a 25 minute ctest
	 * timeout.
	 */
	while (waitpid(pid, &status, WNOHANG) == 0) {
		if (now_ms() - start > PTY_WAIT_MS) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			fprintf(stderr, "probe: child did not answer\n");
			close(master);
			return FYMM_BG_AUTO;
		}
		usleep(10000);
	}
	if (!WIFEXITED(status))
		return FYMM_BG_AUTO;
	*elapsed = now_ms() - start;
	close(master);

	return WIFEXITED(status) ? (enum fymm_background)WEXITSTATUS(status) :
				   FYMM_BG_AUTO;
}

int main(void)
{
	enum fymm_background bg;
	double elapsed = 0.0;

	/* white: a light terminal */
	bg = probe("\033]11;rgb:ffff/ffff/ffff\033\\", &elapsed);
	CHECK(bg == FYMM_BG_LIGHT, "a white background read as %d", (int)bg);

	/* near black: a dark one */
	bg = probe("\033]11;rgb:1e1e/1e1e/1e1e\033\\", &elapsed);
	CHECK(bg == FYMM_BG_DARK, "a near black background read as %d",
	      (int)bg);

	/* the answer may carry one hex digit per component, or four */
	bg = probe("\033]11;rgb:f/f/f\033\a", &elapsed);
	CHECK(bg == FYMM_BG_LIGHT, "a short answer read as %d", (int)bg);

	/* a terminal that never answers must time out, not hang */
	bg = probe(NULL, &elapsed);
	CHECK(bg == FYMM_BG_DARK, "a silent terminal read as %d", (int)bg);
	CHECK(elapsed < 1000.0,
	      "a silent terminal took %.0fms; the query should time out",
	      elapsed);

	/* rubbish is not an answer */
	bg = probe("\033]11;not-a-colour\033\\", &elapsed);
	CHECK(bg == FYMM_BG_DARK, "an unparseable answer read as %d", (int)bg);

	if (failures)
		fprintf(stderr, "%d check(s) failed\n", failures);
	return failures ? 1 : 0;
}
