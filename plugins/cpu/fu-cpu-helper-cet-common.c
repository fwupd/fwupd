/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 H.J. Lu <hjl.tools@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-cpu-helper-cet-common.h"

static void
fu_cpu_helper_cet_testfn_fptr (void)
{
}

static void
__attribute__ ((noinline, noclone))
fu_cpu_helper_cet_testfn_call_fptr (void (*func) (void))
{
	func ();
}

void
__attribute__ ((noinline, noclone))
fu_cpu_helper_cet_testfn1 (void)
{
	fu_cpu_helper_cet_testfn_call_fptr (fu_cpu_helper_cet_testfn_fptr);
}
