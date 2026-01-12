/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <gio/gio.h>
#include <unistd.h>

#include "fu-common-private.h"
#include "fu-kernel.h"
#include "fu-path.h"

#define UDISKS_DBUS_PATH	      "/org/freedesktop/UDisks2"
#define UDISKS_DBUS_MANAGER_PATH      "/org/freedesktop/UDisks2/Manager"
#define UDISKS_DBUS_MANAGER_INTERFACE "org.freedesktop.UDisks2.Manager"

/* required for udisks <= 2.1.7 */
static GPtrArray *
fu_common_get_block_devices_legacy(GError **error)
{
	g_autolist(GDBusObject) dbus_objects = NULL;
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GDBusObjectManager) dbus_object_manager = NULL;
	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL) {
		g_prefix_error_literal(error, "failed to get system bus: ");
		return NULL;
	}
	dbus_object_manager =
	    g_dbus_object_manager_client_new_sync(connection,
						  G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
						  UDISKS_DBUS_SERVICE,
						  UDISKS_DBUS_PATH,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  error);
	if (dbus_object_manager == NULL)
		return NULL;
	dbus_objects = g_dbus_object_manager_get_objects(dbus_object_manager);
	for (GList *l = dbus_objects; l != NULL; l = l->next) {
		GDBusObject *dbus_object = G_DBUS_OBJECT(l->data);
		const gchar *obj = g_dbus_object_get_object_path(dbus_object);
		g_autoptr(GDBusInterface) dbus_iface_blk = NULL;
		g_autoptr(GDBusProxy) proxy_blk = NULL;

		dbus_iface_blk =
		    g_dbus_object_get_interface(dbus_object, UDISKS_DBUS_INTERFACE_BLOCK);
		if (dbus_iface_blk == NULL) {
			g_debug("skipping %s as has no block interface", obj);
			continue;
		}
		proxy_blk = g_dbus_proxy_new_sync(connection,
						  G_DBUS_PROXY_FLAGS_NONE,
						  NULL,
						  UDISKS_DBUS_SERVICE,
						  obj,
						  UDISKS_DBUS_INTERFACE_BLOCK,
						  NULL,
						  error);
		if (proxy_blk == NULL) {
			g_prefix_error(error, "failed to initialize d-bus proxy for %s: ", obj);
			return NULL;
		}
		g_ptr_array_add(devices, g_steal_pointer(&proxy_blk));
	}

	/* success */
	return g_steal_pointer(&devices);
}

GPtrArray *
fu_common_get_block_devices(GError **error)
{
	GVariantBuilder builder;
	const gchar *obj;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) output = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GVariantIter) obj_iter = NULL;
	g_autoptr(GDBusConnection) connection = NULL;

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL) {
		g_prefix_error_literal(error, "failed to get system bus: ");
		return NULL;
	}
	proxy = g_dbus_proxy_new_sync(connection,
				      G_DBUS_PROXY_FLAGS_NONE,
				      NULL,
				      UDISKS_DBUS_SERVICE,
				      UDISKS_DBUS_MANAGER_PATH,
				      UDISKS_DBUS_MANAGER_INTERFACE,
				      NULL,
				      error);
	if (proxy == NULL) {
		g_prefix_error(error, "failed to find %s: ", UDISKS_DBUS_SERVICE);
		return NULL;
	}

	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	output = g_dbus_proxy_call_sync(proxy,
					"GetBlockDevices",
					g_variant_new("(a{sv})", &builder),
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error_local);
	if (output == NULL) {
		if (g_error_matches(error_local, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD)) {
			g_debug("ignoring %s, trying fallback", error_local->message);
			return fu_common_get_block_devices_legacy(error);
		}
		g_dbus_error_strip_remote_error(error_local);
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to call %s.%s(): ",
					   UDISKS_DBUS_MANAGER_INTERFACE,
					   "GetBlockDevices");
		return NULL;
	}

	g_variant_get(output, "(ao)", &obj_iter);
	while (g_variant_iter_next(obj_iter, "&o", &obj)) {
		g_autoptr(GDBusProxy) proxy_blk = NULL;
		proxy_blk = g_dbus_proxy_new_sync(connection,
						  G_DBUS_PROXY_FLAGS_NONE,
						  NULL,
						  UDISKS_DBUS_SERVICE,
						  obj,
						  UDISKS_DBUS_INTERFACE_BLOCK,
						  NULL,
						  error);
		if (proxy_blk == NULL) {
			g_prefix_error(error, "failed to initialize d-bus proxy for %s: ", obj);
			return NULL;
		}
		g_ptr_array_add(devices, g_steal_pointer(&proxy_blk));
	}
	return g_steal_pointer(&devices);
}

guint64
fu_common_get_memory_size_impl(void)
{
	glong phys_pages = sysconf(_SC_PHYS_PAGES);
	glong page_size = sysconf(_SC_PAGE_SIZE);
	if (phys_pages > 0 && page_size > 0)
		return (guint64)phys_pages * (guint64)page_size;
	return 0;
}

gchar *
fu_common_get_kernel_cmdline_impl(GError **error)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GString) cmdline_safe = g_string_new(NULL);
	const gchar *ignore[] = {
	    "",
	    "apparmor",
	    "audit",
	    "auto",
	    "bluetooth.disable_ertm",
	    "boot",
	    "BOOT_IMAGE",
	    "console",
	    "crashkernel",
	    "cryptdevice",
	    "cryptkey",
	    "dm",
	    "earlycon",
	    "earlyprintk",
	    "ether",
	    "init",
	    "initrd",
	    "ip",
	    "LANG",
	    "loglevel",
	    "luks.key",
	    "luks.name",
	    "luks.options",
	    "luks.uuid",
	    "mitigations",
	    "mount.usr",
	    "mount.usrflags",
	    "mount.usrfstype",
	    "netdev",
	    "netroot",
	    "nfsaddrs",
	    "nfs.nfs4_unique_id",
	    "nfsroot",
	    "noplymouth",
	    "nowatchdog",
	    "ostree",
	    "preempt",
	    "quiet",
	    "rd.dm.uuid",
	    "rd.luks.allow-discards",
	    "rd.luks.key",
	    "rd.luks.name",
	    "rd.luks.options",
	    "rd.luks.uuid",
	    "rd.lvm.lv",
	    "rd.lvm.vg",
	    "rd.md.uuid",
	    "rd.systemd.mask",
	    "rd.systemd.wants",
	    "resume",
	    "resumeflags",
	    "rhgb",
	    "ro",
	    "root",
	    "rootflags",
	    "rootfstype",
	    "roothash",
	    "rw",
	    "security",
	    "selinux",
	    "showopts",
	    "splash",
	    "swap",
	    "systemd.machine_id",
	    "systemd.mask",
	    "systemd.show_status",
	    "systemd.unit",
	    "systemd.verity_root_data",
	    "systemd.verity_root_hash",
	    "systemd.wants",
	    "udev.log_priority",
	    "verbose",
	    "vt.handoff",
	    "zfs",
	    "zswap.enabled",
	    NULL, /* last entry */
	};

	/* get a PII-safe kernel command line */
	hash = fu_kernel_get_cmdline(error);
	if (hash == NULL)
		return NULL;
	for (guint i = 0; ignore[i] != NULL; i++)
		g_hash_table_remove(hash, ignore[i]);
	g_hash_table_iter_init(&iter, hash);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (cmdline_safe->len > 0)
			g_string_append(cmdline_safe, " ");
		if (value == NULL) {
			g_string_append(cmdline_safe, (gchar *)key);
			continue;
		}
		g_string_append_printf(cmdline_safe, "%s=%s", (gchar *)key, (gchar *)value);
	}

	return g_string_free(g_steal_pointer(&cmdline_safe), FALSE);
}

gchar *
fu_common_get_olson_timezone_id_impl(GError **error)
{
	g_autofree gchar *fn_localtime = fu_path_from_kind(FU_PATH_KIND_LOCALTIME);
	g_autoptr(GFile) file_localtime = g_file_new_for_path(fn_localtime);

	/* use the last two sections of the symlink target */
	g_debug("looking for timezone file %s", fn_localtime);
	if (g_file_query_file_type(file_localtime, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) ==
	    G_FILE_TYPE_SYMBOLIC_LINK) {
		const gchar *target;
		g_autoptr(GFileInfo) info = NULL;

		info = g_file_query_info(file_localtime,
					 G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
					 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					 NULL,
					 error);
		if (info == NULL)
			return NULL;
		target = g_file_info_get_symlink_target(info);
		if (target != NULL) {
			g_auto(GStrv) sections = g_strsplit(target, "/", -1);
			guint sections_len = g_strv_length(sections);
			if (sections_len < 2) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid symlink target: %s",
					    target);
				return NULL;
			}
			return g_strdup_printf("%s/%s",
					       sections[sections_len - 2],
					       sections[sections_len - 1]);
		}
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no timezone or localtime is available");
	return NULL;
}
