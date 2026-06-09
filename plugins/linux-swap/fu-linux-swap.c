/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <sys/sysmacros.h>

#include "fu-linux-swap.h"

#define FU_LINUX_SWAP_DM_MAX_DEPTH 16

struct _FuLinuxSwap {
	GObject parent_instance;
	guint encrypted_cnt;
	guint enabled_cnt;
};

G_DEFINE_TYPE(FuLinuxSwap, fu_linux_swap, G_TYPE_OBJECT)

static gchar *
fu_linux_swap_strip_spaces(const gchar *line)
{
	GString *str = g_string_new(NULL);
	for (guint i = 0; line[i] != '\0' && !g_ascii_isspace(line[i]); i++)
		g_string_append_c(str, line[i]);
	return g_string_free(str, FALSE);
}

/* walk device-mapper dependencies and report encrypted if any layer has a
 * dm/uuid prefix of CRYPT-; udisks only sees the topmost layer. missing
 * sysfs paths are treated as "no encryption found" */
gboolean
fu_linux_swap_block_has_crypt_below(FuPathStore *pstore,
				    const gchar *name,
				    guint depth,
				    gboolean *encrypted,
				    GError **error)
{
	const gchar *child;
	g_autofree gchar *slaves_path = NULL;
	g_autofree gchar *uuid = NULL;
	g_autofree gchar *uuid_path = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(pstore != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(encrypted != NULL, FALSE);

	if (depth > FU_LINUX_SWAP_DM_MAX_DEPTH) {
		g_debug("sysfs slaves walk hit max depth at %s", name);
		return TRUE;
	}

	uuid_path = fu_path_store_build_filename(pstore,
						 error,
						 FU_PATH_KIND_SYSFSDIR,
						 "class",
						 "block",
						 name,
						 "dm",
						 "uuid",
						 NULL);
	if (uuid_path == NULL)
		return FALSE;
	if (g_file_get_contents(uuid_path, &uuid, NULL, &error_local)) {
		g_strstrip(uuid);
		if (g_str_has_prefix(uuid, "CRYPT-")) {
			*encrypted = TRUE;
			return TRUE;
		}
	} else if (g_error_matches(error_local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_debug("no dm/uuid for %s", name);
		g_clear_error(&error_local);
	} else {
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	slaves_path = fu_path_store_build_filename(pstore,
						   error,
						   FU_PATH_KIND_SYSFSDIR,
						   "class",
						   "block",
						   name,
						   "slaves",
						   NULL);
	if (slaves_path == NULL)
		return FALSE;
	dir = g_dir_open(slaves_path, 0, &error_local);
	if (dir == NULL) {
		if (g_error_matches(error_local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
			g_debug("no slaves dir for %s", name);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	while ((child = g_dir_read_name(dir)) != NULL) {
		if (!fu_linux_swap_block_has_crypt_below(pstore,
							 child,
							 depth + 1,
							 encrypted,
							 error))
			return FALSE;
		if (*encrypted)
			return TRUE;
	}
	return TRUE;
}

/* map dev_t to kernel block name via /sys/dev/block/<major>:<minor> symlink */
static gchar *
fu_linux_swap_block_name_for_devnum(FuPathStore *pstore, guint32 devnum, GError **error)
{
	g_autofree gchar *devkey = NULL;
	g_autofree gchar *symlink_path = NULL;
	g_autofree gchar *target = NULL;

	devkey = g_strdup_printf("%u:%u", major(devnum), minor(devnum));
	symlink_path = fu_path_store_build_filename(pstore,
						    error,
						    FU_PATH_KIND_SYSFSDIR,
						    "dev",
						    "block",
						    devkey,
						    NULL);
	if (symlink_path == NULL)
		return NULL;
	target = g_file_read_link(symlink_path, error);
	if (target == NULL)
		return NULL;
	return g_path_get_basename(target);
}

static gboolean
fu_linux_swap_verify_partition(FuLinuxSwap *self,
			       FuPathStore *pstore,
			       const gchar *fn,
			       GError **error)
{
	gboolean encrypted_below = FALSE;
	g_autofree gchar *name = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GFile) gfile = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GError) error_local = NULL;

	/* this isn't technically encrypted, but isn't on disk in plaintext */
	if (g_str_has_prefix(fn, "/dev/zram")) {
		g_debug("%s is zram, assuming encrypted", fn);
		self->encrypted_cnt++;
		return TRUE;
	}

	/* find the device */
	volume = fu_volume_new_by_device(fn, error);
	if (volume == NULL)
		return FALSE;

	/* udisks sees a direct dm-crypt backing */
	if (fu_volume_is_encrypted(volume)) {
		g_debug("%s partition is encrypted", fn);
		self->encrypted_cnt++;
		return TRUE;
	}

	/* udisks only checks one layer; st_rdev resolves /dev/mapper/<name> symlinks
	 * to the canonical kernel block name (dm-X) */
	gfile = g_file_new_for_path(fn);
	info = g_file_query_info(gfile,
				 G_FILE_ATTRIBUTE_UNIX_RDEV,
				 G_FILE_QUERY_INFO_NONE,
				 NULL,
				 &error_local);
	if (info == NULL) {
		g_debug("could not query rdev for %s: %s", fn, error_local->message);
		g_clear_error(&error_local);
	} else {
		guint32 rdev = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_RDEV);
		if (rdev != 0) {
			name = fu_linux_swap_block_name_for_devnum(pstore, rdev, &error_local);
			if (name == NULL) {
				g_debug("st_rdev mapping failed for %s: %s",
					fn,
					error_local->message);
				g_clear_error(&error_local);
			}
		}
	}
	if (name == NULL) {
		/* st_rdev mapping failed; basename only matches /dev/<kernel-name> */
		name = g_path_get_basename(fn);
		g_debug("falling back to basename %s", name);
	}
	if (!fu_linux_swap_block_has_crypt_below(pstore, name, 0, &encrypted_below, &error_local)) {
		g_debug("dm walk failed for %s: %s", fn, error_local->message);
		g_clear_error(&error_local);
	} else if (encrypted_below) {
		g_debug("%s partition is encrypted via dm chain below %s", fn, name);
		self->encrypted_cnt++;
		return TRUE;
	}

	g_debug("%s partition is unencrypted", fn);
	return TRUE;
}

static gboolean
fu_linux_swap_verify_file(FuLinuxSwap *self, FuPathStore *pstore, const gchar *fn, GError **error)
{
	gboolean encrypted_below = FALSE;
	guint32 devnum;
	g_autofree gchar *name = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GError) error_local = NULL;

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

	/* udisks sees a direct dm-crypt backing */
	if (fu_volume_is_encrypted(volume)) {
		g_debug("%s file is encrypted", fn);
		self->encrypted_cnt++;
		return TRUE;
	}

	/* walk the backing block device for a CRYPT- node */
	name = fu_linux_swap_block_name_for_devnum(pstore, devnum, &error_local);
	if (name == NULL) {
		g_debug("no kernel block name resolved for %s: %s; skipping dm walk",
			fn,
			error_local->message);
		g_clear_error(&error_local);
	} else if (!fu_linux_swap_block_has_crypt_below(pstore,
							name,
							0,
							&encrypted_below,
							&error_local)) {
		g_debug("dm walk failed for %s: %s", fn, error_local->message);
		g_clear_error(&error_local);
	} else if (encrypted_below) {
		g_debug("%s file is encrypted via dm chain below %s", fn, name);
		self->encrypted_cnt++;
		return TRUE;
	}

	g_debug("%s file is unencrypted", fn);
	return TRUE;
}

FuLinuxSwap *
fu_linux_swap_new(FuPathStore *pstore, const gchar *buf, gsize bufsz, GError **error)
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
			fn = fu_linux_swap_strip_spaces(lines[i]);
			ty = fu_linux_swap_strip_spaces(lines[i] + 40);

			/* partition, so use UDisks to see if backed by crypto */
			if (g_strcmp0(ty, "partition") == 0) {
				self->enabled_cnt++;
				if (!fu_linux_swap_verify_partition(self, pstore, fn, error))
					return NULL;
			} else if (g_strcmp0(ty, "file") == 0) {
				g_autofree gchar *path = NULL;

				/* get the path to the file */
				path = fu_path_store_build_filename(pstore,
								    error,
								    FU_PATH_KIND_HOSTFS_ROOT,
								    fn,
								    NULL);

				self->enabled_cnt++;
				if (!fu_linux_swap_verify_file(self, pstore, path, error))
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
