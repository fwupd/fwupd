/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <gio/gio.h>
#include <unistd.h>

#include "fu-common-private.h"
#include "fu-kernel.h"

#define UDISKS_DBUS_PATH	      "/org/freedesktop/UDisks2/Manager"
#define UDISKS_DBUS_MANAGER_INTERFACE "org.freedesktop.UDisks2.Manager"

GPtrArray *
fu_common_get_block_devices(GError **error)
{
	GVariantBuilder builder;
	const gchar *obj;
	g_autoptr(GVariant) output = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GVariantIter) obj_iter = NULL;
	g_autoptr(GDBusConnection) connection = NULL;

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL) {
		g_prefix_error(error, "failed to get system bus: ");
		return NULL;
	}
	proxy = g_dbus_proxy_new_sync(connection,
				      G_DBUS_PROXY_FLAGS_NONE,
				      NULL,
				      UDISKS_DBUS_SERVICE,
				      UDISKS_DBUS_PATH,
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
					error);
	if (output == NULL) {
		if (error != NULL)
			g_dbus_error_strip_remote_error(*error);
		g_prefix_error(error,
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
	return (guint64)sysconf(_SC_PHYS_PAGES) * (guint64)sysconf(_SC_PAGE_SIZE);
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
	    "ostree",
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
	    "roothash",
	    "rw",
	    "security",
	    "showopts",
	    "splash",
	    "swap",
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
