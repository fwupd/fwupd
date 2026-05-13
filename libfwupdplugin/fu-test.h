/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#include "fwupd-test.h"

void
fu_test_loop_run_with_timeout(guint timeout_ms);
void
fu_test_loop_quit(void);
