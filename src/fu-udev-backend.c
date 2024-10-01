/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include <fwupdplugin.h>

#include <glib-unix.h>
#include <glib/gstdio.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-engine-struct.h"
#include "fu-udev-backend.h"
#include "fu-udev-device-private.h"

struct _FuUdevBackend {
	FuBackend parent_instance;
	gint netlink_fd;
	GHashTable *changed_idle_ids; /* sysfs:FuUdevBackendHelper */
	GHashTable *map_paths;	      /* of str:None */
	GPtrArray *dpaux_devices;     /* of FuDpauxDevice */
	guint dpaux_devices_rescan_id;
	gboolean done_coldplug;
};

G_DEFINE_TYPE(FuUdevBackend, fu_udev_backend, FU_TYPE_BACKEND)

#define FU_UDEV_BACKEND_DPAUX_RESCAN_DELAY 5 /* s */

static void
fu_udev_backend_to_string(FuBackend *backend, guint idt, GString *str)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(backend);
	fwupd_codec_string_append_bool(str, idt, "DoneColdplug", self->done_coldplug);
}

static void
fu_udev_backend_rescan_dpaux_device(FuUdevBackend *self, FuDevice *dpaux_device)
{
	FuDevice *device_tmp;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* find the device we enumerated */
	g_debug("looking for %s", fu_device_get_backend_id(dpaux_device));
	device_tmp =
	    fu_backend_lookup_by_id(FU_BACKEND(self), fu_device_get_backend_id(dpaux_device));

	/* open */
	fu_device_probe_invalidate(dpaux_device);
	locker = fu_device_locker_new(dpaux_device, &error_local);
	if (locker == NULL) {
		g_debug("failed to open device %s: %s",
			fu_device_get_backend_id(dpaux_device),
			error_local->message);
		if (device_tmp != NULL)
			fu_backend_device_removed(FU_BACKEND(self), FU_DEVICE(device_tmp));
		return;
	}
	if (device_tmp == NULL) {
		fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(dpaux_device));
		return;
	}
}

static gboolean
fu_udev_backend_rescan_dpaux_devices_cb(gpointer user_data)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(user_data);
	for (guint i = 0; i < self->dpaux_devices->len; i++) {
		FuDevice *dpaux_device = g_ptr_array_index(self->dpaux_devices, i);
		fu_udev_backend_rescan_dpaux_device(self, dpaux_device);
	}
	self->dpaux_devices_rescan_id = 0;
	return FALSE;
}

static void
fu_udev_backend_rescan_dpaux_devices(FuUdevBackend *self)
{
	if (self->dpaux_devices_rescan_id != 0)
		g_source_remove(self->dpaux_devices_rescan_id);
	self->dpaux_devices_rescan_id =
	    g_timeout_add_seconds(FU_UDEV_BACKEND_DPAUX_RESCAN_DELAY,
				  fu_udev_backend_rescan_dpaux_devices_cb,
				  self);
}

static FuUdevDevice *
fu_udev_backend_create_device(FuUdevBackend *self, const gchar *fn, GError **error);

static void
fu_udev_backend_create_ddc_proxy(FuUdevBackend *self, FuUdevDevice *udev_device)
{
	g_autofree gchar *proxy_sysfs_path = NULL;
	g_autofree gchar *proxy_sysfs_real = NULL;
	g_autoptr(FuUdevDevice) proxy = NULL;
	g_autoptr(GError) error_local = NULL;

	proxy_sysfs_path =
	    g_build_filename(fu_udev_device_get_sysfs_path(udev_device), "ddc", NULL);
	proxy_sysfs_real = fu_path_make_absolute(proxy_sysfs_path, &error_local);
	if (proxy_sysfs_real == NULL) {
		g_debug("failed to resolve %s: %s", proxy_sysfs_path, error_local->message);
		return;
	}
	proxy = fu_udev_backend_create_device(self, proxy_sysfs_real, &error_local);
	if (proxy == NULL) {
		g_warning("failed to create DRM DDC device: %s", error_local->message);
		return;
	}
	fu_device_add_private_flag(FU_DEVICE(proxy), FU_I2C_DEVICE_PRIVATE_FLAG_NO_HWID_GUIDS);
	if (!fu_device_probe(FU_DEVICE(proxy), &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT))
			return;
		g_warning("failed to probe DRM DDC device: %s", error_local->message);
		return;
	}
	fu_device_add_private_flag(FU_DEVICE(udev_device), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_set_proxy(FU_DEVICE(udev_device), FU_DEVICE(proxy));
}

static FuDevice *
fu_udev_backend_create_device_for_donor(FuBackend *backend, FuDevice *donor, GError **error)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(backend);
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	g_autoptr(FuDevice) device = NULL;
	GType gtype = FU_TYPE_UDEV_DEVICE;
	struct {
		const gchar *subsystem;
		const gchar *devtype;
		GType gtype;
	} map[] = {
	    {"mei", NULL, FU_TYPE_MEI_DEVICE},
	    {"drm", NULL, FU_TYPE_DRM_DEVICE},
	    {"usb", "usb_device", FU_TYPE_USB_DEVICE},
	    {"i2c", NULL, FU_TYPE_I2C_DEVICE},
	    {"i2c-dev", NULL, FU_TYPE_I2C_DEVICE},
	    {"drm_dp_aux_dev", NULL, FU_TYPE_DPAUX_DEVICE},
	    {"hidraw", NULL, FU_TYPE_HIDRAW_DEVICE},
	    {"block", "disk", FU_TYPE_BLOCK_DEVICE},
	    {"serio", NULL, FU_TYPE_SERIO_DEVICE},
	    {"pci", NULL, FU_TYPE_PCI_DEVICE},
	    {"video4linux", NULL, FU_TYPE_V4L_DEVICE},
	};

	/* ignore zram and loop block devices -- of which there are dozens on systems with snap */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(donor)), "block") == 0) {
		g_autofree gchar *basename =
		    g_path_get_basename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(donor)));
		if (g_str_has_prefix(basename, "zram") || g_str_has_prefix(basename, "loop")) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "ignoring uninteresting %s block device",
				    basename);
			return NULL;
		}
	}

	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(donor)),
			      map[i].subsystem) == 0 &&
		    (map[i].devtype == NULL ||
		     g_strcmp0(fu_udev_device_get_devtype(FU_UDEV_DEVICE(donor)), map[i].devtype) ==
			 0)) {
			gtype = map[i].gtype;
			break;
		}
	}
	if (gtype == FU_TYPE_UDEV_DEVICE) {
		device = g_object_ref(donor);
	} else {
		device = g_object_new(gtype, "backend", FU_BACKEND(self), NULL);
		fu_device_incorporate(device, donor, FU_DEVICE_INCORPORATE_FLAG_ALL);
		if (!fu_device_probe(device, error)) {
			g_prefix_error(error, "failed to probe: ");
			return NULL;
		}
	}

	/* these are used without a subclass */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "msr") == 0)
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(device), FU_IO_CHANNEL_OPEN_FLAG_READ);

	/* the DRM device has a i2c device that is used for communicating with the scaler */
	if (gtype == FU_TYPE_DRM_DEVICE)
		fu_udev_backend_create_ddc_proxy(self, FU_UDEV_DEVICE(device));

	/* notify plugins using fu_plugin_add_udev_subsystem() */
	if (fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)) != NULL) {
		g_autoptr(GPtrArray) possible_plugins =
		    fu_context_get_plugin_names_for_udev_subsystem(
			ctx,
			fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)),
			NULL);
		if (possible_plugins != NULL) {
			for (guint i = 0; i < possible_plugins->len; i++) {
				const gchar *plugin_name = g_ptr_array_index(possible_plugins, i);
				fu_device_add_possible_plugin(device, plugin_name);
			}
		}
	}

	/* set in fu-self-test */
	if (g_getenv("FWUPD_SELF_TEST") != NULL)
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_IS_FAKE);
	return g_steal_pointer(&device);
}

static FuUdevDevice *
fu_udev_backend_create_device(FuUdevBackend *self, const gchar *fn, GError **error)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	g_autoptr(FuUdevDevice) device_donor = fu_udev_device_new(ctx, fn);

	/* use a donor device to probe for the subsystem and devtype */
	if (!fu_device_probe(FU_DEVICE(device_donor), error)) {
		g_prefix_error(error, "failed to probe donor: ");
		return NULL;
	}
	return FU_UDEV_DEVICE(fu_udev_backend_create_device_for_donor(FU_BACKEND(self),
								      FU_DEVICE(device_donor),
								      error));
}

static void
fu_udev_backend_device_add_from_device(FuUdevBackend *self, FuUdevDevice *device)
{
	/* DP AUX devices are *weird* and can only read the DPCD when a DRM device is attached */
	if (g_strcmp0(fu_udev_device_get_subsystem(device), "drm_dp_aux_dev") == 0) {
		/* add and rescan, regardless of if we can open it */
		g_ptr_array_add(self->dpaux_devices, g_object_ref(device));
		fu_udev_backend_rescan_dpaux_devices(self);

		/* open -- this might seem redundant, but it means the device is added at daemon
		 * coldplug rather than a few seconds later */
		if (!self->done_coldplug) {
			g_autoptr(FuDeviceLocker) locker = NULL;
			g_autoptr(GError) error_local = NULL;

			locker = fu_device_locker_new(device, &error_local);
			if (locker == NULL) {
				g_debug("failed to open device %s: %s",
					fu_device_get_backend_id(FU_DEVICE(device)),
					error_local->message);
				return;
			}
			fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(device));
		}
		return;
	}

	/* success */
	fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(device));
}

static void
fu_udev_backend_device_remove(FuUdevBackend *self, const gchar *sysfs_path)
{
	FuDevice *device_tmp;

	/* find the device we enumerated */
	device_tmp = fu_backend_lookup_by_id(FU_BACKEND(self), sysfs_path);
	if (device_tmp != NULL) {
		g_debug("UDEV %s removed", sysfs_path);

		/* rescan all the DP AUX devices if it or any DRM device disappears */
		if (g_ptr_array_remove(self->dpaux_devices, device_tmp) ||
		    g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device_tmp)), "drm") ==
			0) {
			fu_udev_backend_rescan_dpaux_devices(self);
		}
		fu_backend_device_removed(FU_BACKEND(self), device_tmp);
	}
}

typedef struct {
	FuUdevBackend *self;
	FuDevice *device;
	guint idle_id;
} FuUdevBackendHelper;

static void
fu_udev_backend_changed_helper_free(FuUdevBackendHelper *helper)
{
	if (helper->idle_id != 0)
		g_source_remove(helper->idle_id);
	g_object_unref(helper->self);
	g_object_unref(helper->device);
	g_free(helper);
}

static FuUdevBackendHelper *
fu_udev_backend_changed_helper_new(FuUdevBackend *self, FuDevice *device)
{
	FuUdevBackendHelper *helper = g_new0(FuUdevBackendHelper, 1);
	helper->self = g_object_ref(self);
	helper->device = g_object_ref(device);
	return helper;
}

static gboolean
fu_udev_backend_device_changed_cb(gpointer user_data)
{
	FuUdevBackendHelper *helper = (FuUdevBackendHelper *)user_data;
	fu_backend_device_changed(FU_BACKEND(helper->self), helper->device);
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(helper->device)), "drm") != 0)
		fu_udev_backend_rescan_dpaux_devices(helper->self);
	helper->idle_id = 0;
	g_hash_table_remove(helper->self->changed_idle_ids,
			    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(helper->device)));
	return FALSE;
}

static void
fu_udev_backend_device_changed(FuUdevBackend *self, const gchar *sysfs_path)
{
	FuUdevBackendHelper *helper;
	FuDevice *device_tmp;

	/* not a device we enumerated */
	device_tmp = fu_backend_lookup_by_id(FU_BACKEND(self), sysfs_path);
	if (device_tmp == NULL)
		return;

	/* run all plugins, with per-device rate limiting */
	if (g_hash_table_remove(self->changed_idle_ids, sysfs_path)) {
		g_debug("re-adding rate-limited timeout for %s", sysfs_path);
	} else {
		g_debug("adding rate-limited timeout for %s", sysfs_path);
	}
	helper = fu_udev_backend_changed_helper_new(self, device_tmp);
	helper->idle_id = g_timeout_add(500, fu_udev_backend_device_changed_cb, helper);
	g_hash_table_insert(self->changed_idle_ids, g_strdup(sysfs_path), helper);
}

static gint
fu_udev_backend_device_number_sort_cb(gconstpointer a, gconstpointer b)
{
	FuUdevDevice *device1 = *((FuUdevDevice **)a);
	FuUdevDevice *device2 = *((FuUdevDevice **)b);
	if (fu_udev_device_get_number(device1) < fu_udev_device_get_number(device2))
		return -1;
	if (fu_udev_device_get_number(device1) > fu_udev_device_get_number(device2))
		return 1;
	return 0;
}

static void
fu_udev_backend_coldplug_subsystem(FuUdevBackend *self, const gchar *fn)
{
	const gchar *basename;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error_dir = NULL;
	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	dir = g_dir_open(fn, 0, &error_dir);
	if (dir == NULL) {
		if (!g_error_matches(error_dir, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			g_debug("ignoring: %s", error_dir->message);
		return;
	}
	while ((basename = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *fn_full = g_build_filename(fn, basename, NULL);
		g_autofree gchar *fn_real = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FuUdevDevice) device = NULL;

		if (!g_file_test(fn_full, G_FILE_TEST_IS_DIR))
			continue;
		fn_real = fu_path_make_absolute(fn_full, &error_local);
		if (fn_real == NULL) {
			g_warning("failed to get symlink target for %s: %s",
				  fn_real,
				  error_local->message);
			continue;
		}
		if (g_hash_table_contains(self->map_paths, fn_real)) {
			g_debug("skipping duplicate %s", fn_real);
			continue;
		}
		device = fu_udev_backend_create_device(self, fn_real, &error_local);
		if (device == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED))
				continue;
			g_warning("failed to create device from %s: %s",
				  fn_real,
				  error_local->message);
			continue;
		}
		g_hash_table_add(self->map_paths, g_steal_pointer(&fn_real));
		g_ptr_array_add(devices, g_steal_pointer(&device));
	}

	/* sort by device order (so video0 comes before video9) and add as a device */
	g_ptr_array_sort(devices, fu_udev_backend_device_number_sort_cb);
	for (guint i = 0; i < devices->len; i++) {
		FuUdevDevice *device = g_ptr_array_index(devices, i);
		fu_udev_backend_device_add_from_device(self, device);
	}
}

static gboolean
fu_udev_backend_netlink_parse_blob(FuUdevBackend *self, GBytes *blob, GError **error)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	FuUdevAction action = FU_UDEV_ACTION_UNKNOWN;
	const guint8 *buf;
	gsize bufsz = 0;
	g_autofree gchar *sysfsdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR);
	g_autoptr(FuStructUdevMonitorNetlinkHeader) st_hdr = NULL;
	g_autoptr(FuUdevDevice) device_actual = NULL;
	g_autoptr(FuUdevDevice) device_donor = NULL;
	g_autoptr(GBytes) blob_payload = NULL;

	/* parse the buffer */
	st_hdr = fu_struct_udev_monitor_netlink_header_parse_bytes(blob, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	blob_payload =
	    fu_bytes_new_offset(blob,
				fu_struct_udev_monitor_netlink_header_get_properties_off(st_hdr),
				fu_struct_udev_monitor_netlink_header_get_properties_len(st_hdr),
				error);
	if (blob_payload == NULL)
		return FALSE;

	/* split into lines */
	buf = g_bytes_get_data(blob_payload, &bufsz);
	for (gsize i = 0; i < bufsz; i++) {
		g_autofree gchar *kvstr = NULL;
		g_auto(GStrv) kv = NULL;

		kvstr = fu_strsafe((const gchar *)buf + i, bufsz - i);
		if (kvstr == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "invalid ACSII buffer");
			return FALSE;
		}
		kv = g_strsplit(kvstr, "=", 2);
		if (g_strcmp0(kv[0], "ACTION") == 0) {
			action = fu_udev_action_from_string(kv[1]);
			if (action == FU_UDEV_ACTION_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "unknown action %s",
					    kv[1]);
				return FALSE;
			}

			/* we do not care about these */
			if (action == FU_UDEV_ACTION_BIND || action == FU_UDEV_ACTION_UNBIND)
				return TRUE;
		} else if (g_strcmp0(kv[0], "DEVPATH") == 0) {
			g_autofree gchar *sysfspath = g_build_filename(sysfsdir, kv[1], NULL);

			/* something changed */
			if (action == FU_UDEV_ACTION_CHANGE) {
				fu_udev_backend_device_changed(self, sysfspath);
				return TRUE;
			}

			/* something got removed */
			if (action == FU_UDEV_ACTION_REMOVE) {
				fu_udev_backend_device_remove(self, sysfspath);
				return TRUE;
			}

			/* something got added */
			if (action == FU_UDEV_ACTION_ADD) {
				if (device_donor != NULL) {
					g_set_error_literal(error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_INVALID_DATA,
							    "already have a donor device");
					return FALSE;
				}
				device_donor = fu_udev_device_new(ctx, sysfspath);
			}
		} else if (g_strcmp0(kv[0], "SUBSYSTEM") == 0 && device_donor != NULL) {
			fu_udev_device_set_subsystem(device_donor, kv[1]);
		} else if (g_strcmp0(kv[0], "DEVTYPE") == 0 && device_donor != NULL) {
			g_object_set(device_donor, "devtype", kv[1], NULL);
		} else if (device_donor != NULL) {
			fu_udev_device_add_property(device_donor, kv[0], kv[1]);
		}

		/* next! */
		i += strlen(kvstr);
	}

	/* we never saw add */
	if (device_donor == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no new device to add");
		return FALSE;
	}

	/* now create the actual device from the donor */
	device_actual =
	    FU_UDEV_DEVICE(fu_udev_backend_create_device_for_donor(FU_BACKEND(self),
								   FU_DEVICE(device_donor),
								   error));
	if (device_actual == NULL)
		return FALSE;

	/* success */
	fu_udev_backend_device_add_from_device(self, device_actual);
	return TRUE;
}

static gboolean
fu_udev_backend_netlink_cb(gint fd, GIOCondition condition, gpointer user_data)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(user_data);
	gssize len;
	guint8 buf[10240] = {0x0};
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	len = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
	if (len < 0)
		return TRUE;
	blob = g_bytes_new(buf, len);
	if (!fu_udev_backend_netlink_parse_blob(self, blob, &error_local)) {
		g_warning("ignoring netlink message: %s", error_local->message);
		return TRUE;
	}
	return TRUE;
}

static gboolean
fu_udev_backend_netlink_setup(FuUdevBackend *self, GError **error)
{
	struct sockaddr_nl nls = {
	    .nl_family = AF_NETLINK,
	    .nl_pid = getpid(),
	    .nl_groups = FU_UDEV_MONITOR_NETLINK_GROUP_UDEV,
	};
	g_autoptr(GSource) source = NULL;

	self->netlink_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
	if (self->netlink_fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to connect to netlink: %s",
			    g_strerror(errno));
		return FALSE;
	}
	if (bind(self->netlink_fd, (void *)&nls, sizeof(nls))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "bind to udev socket failed: %s",
			    g_strerror(errno));
		return FALSE;
	}
	source = g_unix_fd_source_new(self->netlink_fd, G_IO_IN);
	g_source_set_callback(source, (GSourceFunc)fu_udev_backend_netlink_cb, self, NULL);
	g_source_attach(source, NULL);

	/* success */
	return TRUE;
}

static gboolean
fu_udev_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_backend_get_context(backend);
	FuUdevBackend *self = FU_UDEV_BACKEND(backend);
	g_autofree gchar *sysfsdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR);
	g_autoptr(GPtrArray) udev_subsystems = fu_context_get_udev_subsystems(ctx);

	/* get all devices of class */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, udev_subsystems->len);
	for (guint i = 0; i < udev_subsystems->len; i++) {
		const gchar *subsystem = g_ptr_array_index(udev_subsystems, i);
		g_autofree gchar *class_fn = NULL;
		g_autofree gchar *bus_fn = NULL;

		class_fn = g_build_filename(sysfsdir, "class", subsystem, NULL);
		if (g_file_test(class_fn, G_FILE_TEST_EXISTS)) {
			fu_udev_backend_coldplug_subsystem(self, class_fn);
			fu_progress_step_done(progress);
			continue;
		}
		bus_fn = g_build_filename(sysfsdir, "bus", subsystem, "devices", NULL);
		if (g_file_test(bus_fn, G_FILE_TEST_EXISTS)) {
			fu_udev_backend_coldplug_subsystem(self, bus_fn);
			fu_progress_step_done(progress);
			continue;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	self->done_coldplug = TRUE;
	return TRUE;
}

static gboolean
fu_udev_backend_setup(FuBackend *backend,
		      FuBackendSetupFlags flags,
		      FuProgress *progress,
		      GError **error)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(backend);

	/* set up hotplug events */
	if (flags & FU_BACKEND_SETUP_FLAG_USE_HOTPLUG) {
		if (!fu_udev_backend_netlink_setup(self, error)) {
			g_prefix_error(error, "failed to set up netlink: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static FuDevice *
fu_udev_backend_get_device_parent(FuBackend *backend,
				  FuDevice *device,
				  const gchar *subsystem,
				  GError **error)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(backend);
	g_autofree gchar *devtype_new = NULL;
	g_autofree gchar *sysfs_path = NULL;
	g_autoptr(FuUdevDevice) device_new = NULL;

	/* emulated */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return g_object_ref(device);

	sysfs_path = g_strdup(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
	if (sysfs_path == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "sysfs path undefined");
		return NULL;
	}

	if (!g_file_test(sysfs_path, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "sysfs path '%s' doesn't exist, perhaps a transient device.",
			    sysfs_path);
		return NULL;
	}

	/* lets just walk up the directories */
	while (1) {
		g_autofree gchar *dirname = NULL;
		g_autoptr(GError) error_local = NULL;

		/* done? */
		dirname = g_path_get_dirname(sysfs_path);
		if (g_strcmp0(dirname, ".") == 0 || g_strcmp0(dirname, "/") == 0)
			break;

		/* check has matching subsystem and devtype */
		device_new = fu_udev_backend_create_device(self, dirname, &error_local);
		if (device_new != NULL) {
			if (fu_udev_device_match_subsystem(device_new, subsystem)) {
				if (subsystem != NULL) {
					g_auto(GStrv) split = g_strsplit(subsystem, ":", 2);
					fu_udev_device_set_subsystem(device_new, split[0]);
				}
				return FU_DEVICE(g_steal_pointer(&device_new));
			}
		} else {
			if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
				g_warning("failed to create device: %s", error_local->message);
		}

		/* just swap, and go deeper */
		g_free(sysfs_path);
		sysfs_path = g_steal_pointer(&dirname);
	}

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "no parent with subsystem %s",
		    subsystem);
	return NULL;
}

static FuDevice *
fu_udev_backend_create_device_impl(FuBackend *backend, const gchar *backend_id, GError **error)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(backend);
	return FU_DEVICE(fu_udev_backend_create_device(self, backend_id, error));
}

static void
fu_udev_backend_finalize(GObject *object)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(object);
	if (self->dpaux_devices_rescan_id != 0)
		g_source_remove(self->dpaux_devices_rescan_id);
	if (self->netlink_fd > 0)
		g_close(self->netlink_fd, NULL);
	g_hash_table_unref(self->changed_idle_ids);
	g_hash_table_unref(self->map_paths);
	g_ptr_array_unref(self->dpaux_devices);
	G_OBJECT_CLASS(fu_udev_backend_parent_class)->finalize(object);
}

static void
fu_udev_backend_init(FuUdevBackend *self)
{
	self->map_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	self->dpaux_devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->changed_idle_ids =
	    g_hash_table_new_full(g_str_hash,
				  g_str_equal,
				  g_free,
				  (GDestroyNotify)fu_udev_backend_changed_helper_free);
}

static void
fu_udev_backend_class_init(FuUdevBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);
	object_class->finalize = fu_udev_backend_finalize;
	backend_class->coldplug = fu_udev_backend_coldplug;
	backend_class->setup = fu_udev_backend_setup;
	backend_class->to_string = fu_udev_backend_to_string;
	backend_class->get_device_parent = fu_udev_backend_get_device_parent;
	backend_class->create_device = fu_udev_backend_create_device_impl;
	backend_class->create_device_for_donor = fu_udev_backend_create_device_for_donor;
}

FuBackend *
fu_udev_backend_new(FuContext *ctx)
{
	return FU_BACKEND(g_object_new(FU_TYPE_UDEV_BACKEND,
				       "name",
				       "udev",
				       "context",
				       ctx,
				       "device-gtype",
				       FU_TYPE_UDEV_DEVICE,
				       NULL));
}
