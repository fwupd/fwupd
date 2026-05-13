/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 H.J. Lu <hjl.tools@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <signal.h>
#include <stdlib.h>

#include "fu-cpu-helper-cet-common.h"

#ifdef HAVE_SIGACTION
static __attribute__((noreturn)) void
fu_cpu_helper_cet_segfault_sigaction(int signal, siginfo_t *si, void *arg)
{
	/* CET did exactly as it should to protect the system */
	exit(0);
}
#endif

int
main(int argc, char *argv[])
{
#ifdef HAVE_SIGACTION
	struct sigaction sa = {0};

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = fu_cpu_helper_cet_segfault_sigaction;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
#endif

	fu_cpu_helper_cet_testfn1();

	/* this means CET did not work */
	return 1;
}
