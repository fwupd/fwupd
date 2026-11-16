/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>

#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif
#ifdef HAVE_LINUX_BTRFS_H
#include <linux/btrfs.h>
#include <linux/magic.h>
#endif

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
	return g_string_free_and_steal(str);
}

/* whether the device is a dm-crypt device, by its dm/uuid prefix of CRYPT- */
static gboolean
fu_linux_swap_block_is_crypt(FuPathStore *pstore,
			     const gchar *name,
			     gboolean *is_crypt,
			     GError **error)
{
	g_autofree gchar *uuid = NULL;
	g_autofree gchar *uuid_path = NULL;
	g_autoptr(GError) error_local = NULL;

	*is_crypt = FALSE;
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
	if (!g_file_get_contents(uuid_path, &uuid, NULL, &error_local)) {
		if (g_error_matches(error_local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
			g_debug("no dm/uuid for %s", name);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	g_strstrip(uuid);
	*is_crypt = g_str_has_prefix(uuid, "CRYPT-");
	return TRUE;
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
	gboolean is_crypt = FALSE;
	g_autofree gchar *slaves_path = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(pstore != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(encrypted != NULL, FALSE);

	if (depth > FU_LINUX_SWAP_DM_MAX_DEPTH) {
		g_debug("sysfs slaves walk hit max depth at %s", name);
		return TRUE;
	}

	if (!fu_linux_swap_block_is_crypt(pstore, name, &is_crypt, error))
		return FALSE;
	if (is_crypt) {
		*encrypted = TRUE;
		return TRUE;
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

/* like fu_linux_swap_block_has_crypt_below(), but encrypted only if every branch
 * below the device has a crypt layer, as a device-mapper or md device can join an
 * encrypted and a plain device, and either can hold swap */
static gboolean
fu_linux_swap_block_has_crypt_on_all(FuPathStore *pstore,
				     const gchar *name,
				     guint depth,
				     gboolean *encrypted,
				     GError **error)
{
	const gchar *child;
	gboolean is_crypt = FALSE;
	guint children_cnt = 0;
	g_autofree gchar *slaves_path = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error_local = NULL;

	*encrypted = FALSE;
	if (depth > FU_LINUX_SWAP_DM_MAX_DEPTH) {
		g_debug("sysfs slaves walk hit max depth at %s", name);
		return TRUE;
	}

	if (!fu_linux_swap_block_is_crypt(pstore, name, &is_crypt, error))
		return FALSE;
	if (is_crypt) {
		*encrypted = TRUE;
		return TRUE;
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
		gboolean encrypted_child = FALSE;
		if (!fu_linux_swap_block_has_crypt_on_all(pstore,
							  child,
							  depth + 1,
							  &encrypted_child,
							  error))
			return FALSE;
		if (!encrypted_child) {
			g_debug("%s has no crypt layer below %s", name, child);
			return TRUE;
		}
		children_cnt++;
	}
	*encrypted = children_cnt > 0;
	return TRUE;
}

/* encrypted only if every device of the btrfs filesystem has a crypt layer below
 * it, as a filesystem can span several devices and any of them can hold swap */
gboolean
fu_linux_swap_btrfs_has_crypt(FuPathStore *pstore,
			      const gchar *fsid,
			      gboolean *encrypted,
			      GError **error)
{
	const gchar *name;
	guint devices_cnt = 0;
	g_autofree gchar *devices_path = NULL;
	g_autoptr(GDir) dir = NULL;

	g_return_val_if_fail(pstore != NULL, FALSE);
	g_return_val_if_fail(fsid != NULL, FALSE);
	g_return_val_if_fail(encrypted != NULL, FALSE);

	*encrypted = FALSE;
	devices_path = fu_path_store_build_filename(pstore,
						    error,
						    FU_PATH_KIND_SYSFSDIR,
						    "fs",
						    "btrfs",
						    fsid,
						    "devices",
						    NULL);
	if (devices_path == NULL)
		return FALSE;
	dir = g_dir_open(devices_path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((name = g_dir_read_name(dir)) != NULL) {
		gboolean encrypted_below = FALSE;
		if (!fu_linux_swap_block_has_crypt_on_all(pstore, name, 0, &encrypted_below, error))
			return FALSE;
		if (!encrypted_below) {
			g_debug("btrfs %s device %s is unencrypted", fsid, name);
			return TRUE;
		}
		devices_cnt++;
	}
	*encrypted = devices_cnt > 0;
	return TRUE;
}

/* btrfs subvolumes have anonymous device numbers without a block device, so ask
 * btrfs for the fsid that /sys/fs/btrfs lists the devices of the filesystem under */
static gchar *
fu_linux_swap_btrfs_fsid_for_file(const gchar *fn, GError **error)
{
#if defined(HAVE_LINUX_BTRFS_H) && defined(HAVE_SYS_VFS_H)
	struct btrfs_ioctl_fs_info_args args = {0};
	struct statfs buf = {0};
	gint rc;
	g_autofd gint fd = -1;

	if (statfs(fn, &buf) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to get the filesystem of %s: %s",
			    fn,
			    fwupd_strerror(errno));
		return NULL;
	}
	if ((guint32)buf.f_type != BTRFS_SUPER_MAGIC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s is not on btrfs",
			    fn);
		return NULL;
	}

	/* the ioctl reports the filesystem of the opened file, so open the swap file itself:
	 * its directory can be on another filesystem, e.g. for a bind-mounted swap file */
	fd = open(fn, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to open %s: %s",
			    fn,
			    fwupd_strerror(errno));
		return NULL;
	}
	rc = ioctl(fd, BTRFS_IOC_FS_INFO, &args); /* nocheck:blocked */
	if (rc != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to get the btrfs fsid of %s: %s",
			    fn,
			    fwupd_strerror(errno));
		return NULL;
	}
	return fwupd_guid_to_string((const fwupd_guid_t *)args.fsid, FWUPD_GUID_FLAG_NONE);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "btrfs is not supported");
	return NULL;
#endif
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
	g_autoptr(GError) error_volume = NULL;
	g_autoptr(GError) error_rdev = NULL;
	g_autoptr(GError) error_name = NULL;
	g_autoptr(GError) error_walk = NULL;

	/* this isn't technically encrypted, but isn't on disk in plaintext */
	if (g_str_has_prefix(fn, "/dev/zram")) {
		g_debug("%s is zram, assuming encrypted", fn);
		self->encrypted_cnt++;
		return TRUE;
	}

	/* a missing volume is not fatal; fall back to the sysfs walk below */
	volume = fu_volume_new_by_device(fn, &error_volume);
	if (volume == NULL) {
		g_debug("no udisks volume for %s: %s", fn, error_volume->message);
	} else if (fu_volume_is_encrypted(volume)) {
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
				 &error_rdev);
	if (info == NULL) {
		g_debug("could not query rdev for %s: %s", fn, error_rdev->message);
	} else {
		guint32 rdev = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_RDEV);
		if (rdev != 0) {
			name = fu_linux_swap_block_name_for_devnum(pstore, rdev, &error_name);
			if (name == NULL) {
				g_debug("st_rdev mapping failed for %s: %s",
					fn,
					error_name->message);
			}
		}
	}
	if (name == NULL) {
		/* st_rdev mapping failed; basename only matches /dev/<kernel-name> */
		name = g_path_get_basename(fn);
		g_debug("falling back to basename %s", name);
	}
	if (!fu_linux_swap_block_has_crypt_below(pstore, name, 0, &encrypted_below, &error_walk)) {
		g_debug("dm walk failed for %s: %s", fn, error_walk->message);
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
	g_autofree gchar *fsid = NULL;
	g_autofree gchar *name = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GError) error_volume = NULL;
	g_autoptr(GError) error_name = NULL;
	g_autoptr(GError) error_fsid = NULL;
	g_autoptr(GError) error_walk = NULL;

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

	/* a missing volume is not fatal; fall back to the sysfs walk below */
	volume = fu_volume_new_by_devnum(devnum, &error_volume);
	if (volume == NULL) {
		g_debug("no udisks volume for %s: %s", fn, error_volume->message);
	} else if (fu_volume_is_encrypted(volume)) {
		g_debug("%s file is encrypted", fn);
		self->encrypted_cnt++;
		return TRUE;
	}

	/* walk the backing block device for a CRYPT- node */
	name = fu_linux_swap_block_name_for_devnum(pstore, devnum, &error_name);
	if (name == NULL) {
		g_debug("no kernel block name resolved for %s: %s", fn, error_name->message);
		fsid = fu_linux_swap_btrfs_fsid_for_file(fn, &error_fsid);
		if (fsid == NULL) {
			g_debug("no btrfs fsid for %s: %s; skipping dm walk",
				fn,
				error_fsid->message);
		} else if (!fu_linux_swap_btrfs_has_crypt(pstore,
							  fsid,
							  &encrypted_below,
							  &error_walk)) {
			g_debug("btrfs device walk failed for %s: %s", fn, error_walk->message);
		} else if (encrypted_below) {
			g_debug("%s file is encrypted via all devices of btrfs %s", fn, fsid);
			self->encrypted_cnt++;
			return TRUE;
		}
	} else if (!fu_linux_swap_block_has_crypt_below(pstore,
							name,
							0,
							&encrypted_below,
							&error_walk)) {
		g_debug("dm walk failed for %s: %s", fn, error_walk->message);
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
				if (path == NULL)
					return NULL;

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
