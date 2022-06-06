/*
 * Copyright (C) 2011 Lennart Poettering <lennart@poettering.net>
 * Copyright (C) 2012 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <fwupdplugin.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fu-polkit-agent.h"

static pid_t agent_pid = 0;

static int
fork_agent(pid_t *pid, const char *path, ...)
{
	char **l;
	gboolean stderr_is_tty;
	gboolean stdout_is_tty;
	int fd;
	pid_t n_agent_pid;
	pid_t parent_pid;
	unsigned n, i;
	va_list ap;

	g_return_val_if_fail(pid != 0, 0);
	g_assert(path);

	parent_pid = getpid();

	/* spawns a temporary TTY agent, making sure it goes away when
	 * we go away */
	n_agent_pid = fork();
	if (n_agent_pid < 0)
		return -errno;

	if (n_agent_pid != 0) {
		*pid = n_agent_pid;
		return 0;
	}

#ifdef __linux__
	/* make sure the agent goes away when the parent dies */
	if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
		_exit(EXIT_FAILURE);
#endif
	/* check whether our parent died before we were able
	 * to set the death signal */
	if (getppid() != parent_pid)
		_exit(EXIT_SUCCESS);

	/* TODO: it might be more clean to close all FDs so we don't leak them to the agent */
	stdout_is_tty = isatty(STDOUT_FILENO);
	stderr_is_tty = isatty(STDERR_FILENO);

	if (!stdout_is_tty || !stderr_is_tty) {
		/* Detach from stdout/stderr. and reopen
		 * /dev/tty for them. This is important to
		 * ensure that when systemctl is started via
		 * popen() or a similar call that expects to
		 * read EOF we actually do generate EOF and
		 * not delay this indefinitely by because we
		 * keep an unused copy of stdin around. */
		fd = open("/dev/tty", O_WRONLY);
		if (fd < 0) {
			g_error("Failed to open /dev/tty: %m");
			_exit(EXIT_FAILURE);
		}
		if (!stdout_is_tty)
			dup2(fd, STDOUT_FILENO);
		if (!stderr_is_tty)
			dup2(fd, STDERR_FILENO);
		if (fd > 2)
			close(fd);
	}

	/* count arguments */
	va_start(ap, path);
	for (n = 0; va_arg(ap, char *); n++)
		;
	va_end(ap);

	/* allocate strv */
	l = alloca(sizeof(char *) * (n + 1));

	/* fill in arguments */
	va_start(ap, path);
	for (i = 0; i <= n; i++)
		l[i] = va_arg(ap, char *);
	va_end(ap);

	execv(path, l);
	_exit(EXIT_FAILURE);
}

static int
close_nointr(int fd)
{
	g_assert(fd >= 0);
	for (;;) {
		int r;
		r = close(fd);
		if (r >= 0)
			return r;
		if (errno != EINTR)
			return -errno;
	}
}

static void
close_nointr_nofail(int fd)
{
	int saved_errno = errno;
	/* cannot fail, and guarantees errno is unchanged */
	g_assert(close_nointr(fd) == 0);
	errno = saved_errno;
}

static int
fd_wait_for_event(int fd, int event, uint64_t t)
{
	struct pollfd pollfd = {0};
	int r;

	pollfd.fd = fd;
	pollfd.events = event;
	r = poll(&pollfd, 1, t == (uint64_t)-1 ? -1 : (int)(t / 1000));
	if (r < 0)
		return -errno;
	if (r == 0)
		return 0;

	return pollfd.revents;
}

static int
wait_for_terminate(pid_t pid)
{
	g_return_val_if_fail(pid >= 1, 0);

	for (;;) {
		int status;
		if (waitpid(pid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		return 0;
	}
}

gboolean
fu_polkit_agent_open(GError **error)
{
	int r;
	int pipe_fd[2];
	g_autofree gchar *notify_fd = NULL;
	g_autofree gchar *pkttyagent_fn = NULL;

	if (agent_pid > 0)
		return TRUE;

	/* find binary */
	pkttyagent_fn = fu_path_find_program("pkttyagent", error);
	if (pkttyagent_fn == NULL)
		return FALSE;

	/* check STDIN here, not STDOUT, since this is about input, not output */
	if (!isatty(STDIN_FILENO))
		return TRUE;
	if (pipe(pipe_fd) < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to create pipe: %s",
			    strerror(-errno));
		return FALSE;
	}

	/* fork pkttyagent */
	notify_fd = g_strdup_printf("%i", pipe_fd[1]);
	r = fork_agent(&agent_pid,
		       pkttyagent_fn,
		       pkttyagent_fn,
		       "--notify-fd",
		       notify_fd,
		       "--fallback",
		       NULL);
	if (r < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to fork TTY ask password agent: %s",
			    strerror(-r));
		close_nointr_nofail(pipe_fd[1]);
		close_nointr_nofail(pipe_fd[0]);
		return FALSE;
	}

	/* close the writing side, because that is the one for the agent */
	close_nointr_nofail(pipe_fd[1]);

	/* wait until the agent closes the fd */
	fd_wait_for_event(pipe_fd[0], POLLHUP, (uint64_t)-1);

	close_nointr_nofail(pipe_fd[0]);
	return TRUE;
}

void
fu_polkit_agent_close(void)
{
	if (agent_pid <= 0)
		return;

	/* inform agent that we are done */
	kill(agent_pid, SIGTERM);
	kill(agent_pid, SIGCONT);
	wait_for_terminate(agent_pid);
	agent_pid = 0;
}
