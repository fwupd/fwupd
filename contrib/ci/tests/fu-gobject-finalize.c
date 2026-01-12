/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: did not have parent ->finalize()
 */

static void
fu_gobject_finalize(GObject *object);

static void
fu_gobject_finalize(GObject *object)
{
	g_free(whatever);
	G_OBJECT_CLASS(fu_device_parent_class)->destroy(object);
}
