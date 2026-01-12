/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: invalid function name
 * nocheck:expect: mixed case function name
 * nocheck:expect: function should be called ensure
 */

#define FU_COMMON_VERSION_DECODE_BCD(val) 43

G_DEFINE_TYPE_EXTENDED(FuBackend,
		       fu_backend,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FuBackend)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_backend_codec_iface_init));

typedef guint8 *(*FuConvertFunc)(gpointer user_data);

static void
wrong_name_test(void)
{
}

static gboolean
fu_function_name_set_version(FuExample *self, GError **error)
{
}

static gboolean
fu_function_name_Enter_SBL(FuExample *self, GError **error)
{
}

const gchar *
fwupd_strerror(gint errnum) /* nocheck:name */
{
}
