/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-linux-swap.h"

struct _FuLinuxSwap {
	GObject parent_instance;
	guint encrypted_cnt;
	guint enabled_cnt;
};

G_DEFINE_TYPE(FuLinuxSwap, fu_linux_swap, G_TYPE_OBJECT)

static gchar *
fu_strdup_nospaces(const gchar *line)
{
	GString *str = g_string_new(NULL);
	for (guint i = 0; line[i] != '\0' && !g_ascii_isspace(line[i]); i++)
		g_string_append_c(str, line[i]);
	return g_string_free(str, FALSE);
}

static gboolean
fu_linux_swap_verify_partition(FuLinuxSwap *self, const gchar *fn, GError **error)
{
	g_autoptr(FuVolume) volume = NULL;

	/* find the device */
	volume = fu_volume_new_by_device(fn, error);
	if (volume == NULL)
		return FALSE;

	/* this isn't technically encrypted, but isn't on disk in plaintext */
	if (g_str_has_prefix(fn, "/dev/zram")) {
		g_debug("%s is zram, assuming encrypted", fn);
		self->encrypted_cnt++;
		return TRUE;
	}

	/* is this mount point encrypted */
	if (fu_volume_is_encrypted(volume)) {
		g_debug("%s partition is encrypted", fn);
		self->encrypted_cnt++;
	} else {
		g_debug("%s partition is unencrypted", fn);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_linux_swap_verify_file(FuLinuxSwap *self, const gchar *fn, GError **error)
{
	guint32 devnum;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(FuVolume) volume = NULL;

	/* get the device number for the file */
	file = g_file_new_for_path(fn);
	info = g_file_query_info(file,
				 G_FILE_ATTRIBUTE_UNIX_DEVICE,
				 G_FILE_QUERY_INFO_NONE,
				 NULL,
				 error);
	if (info == NULL)
		return FALSE;
	devnum = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_DEVICE);

	/* find the device */
	volume = fu_volume_new_by_devnum(devnum, error);
	if (volume == NULL)
		return FALSE;

	/* is this mount point encrypted */
	if (fu_volume_is_encrypted(volume)) {
		g_debug("%s file is encrypted", fn);
		self->encrypted_cnt++;
	} else {
		g_debug("%s file is unencrypted", fn);
	}

	/* success */
	return TRUE;
}

FuLinuxSwap *
fu_linux_swap_new(const gchar *buf, gsize bufsz, GError **error)
{
	g_autoptr(FuLinuxSwap) self = g_object_new(FU_TYPE_LINUX_SWAP, NULL);
	g_auto(GStrv) lines = NULL;

	/* look at each line in /proc/swaps */
	if (bufsz == 0)
		bufsz = strlen(buf);
	lines = fu_strsplit(buf, bufsz, "\n", -1);
	if (g_strv_length(lines) > 2) {
		for (guint i = 1; lines[i] != NULL && lines[i][0] != '\0'; i++) {
			g_autofree gchar *fn = NULL;
			g_autofree gchar *ty = NULL;

			/* split */
			if (g_utf8_strlen(lines[i], -1) < 45)
				continue;
			fn = fu_strdup_nospaces(lines[i]);
			ty = fu_strdup_nospaces(lines[i] + 40);

			/* partition, so use UDisks to see if backed by crypto */
			if (g_strcmp0(ty, "partition") == 0) {
				self->enabled_cnt++;
				if (!fu_linux_swap_verify_partition(self, fn, error))
					return NULL;
			} else if (g_strcmp0(ty, "file") == 0) {
				self->enabled_cnt++;
				if (!fu_linux_swap_verify_file(self, fn, error))
					return NULL;
			} else {
				g_warning("unknown swap type: %s [%s]", ty, fn);
			}
		}
	}
	return g_steal_pointer(&self);
}

/* success if *all* the swap devices are encrypted */
gboolean
fu_linux_swap_get_encrypted(FuLinuxSwap *self)
{
	g_return_val_if_fail(FU_IS_LINUX_SWAP(self), FALSE);
	return self->enabled_cnt > 0 && self->enabled_cnt == self->encrypted_cnt;
}

gboolean
fu_linux_swap_get_enabled(FuLinuxSwap *self)
{
	g_return_val_if_fail(FU_IS_LINUX_SWAP(self), FALSE);
	return self->enabled_cnt > 0;
}

static void
fu_linux_swap_class_init(FuLinuxSwapClass *klass)
{
}

static void
fu_linux_swap_init(FuLinuxSwap *self)
{
}
