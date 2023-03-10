/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuVolume"

#include "config.h"

#include <gio/gio.h>

#include "fwupd-error.h"

#include "fu-common-private.h"
#include "fu-volume-private.h"

/**
 * FuVolume:
 *
 * Volume abstraction that uses UDisks
 */

struct _FuVolume {
	GObject parent_instance;
	gchar *partition_kind;
	GDBusProxy *proxy_blk;
	GDBusProxy *proxy_fs;
	gchar *mount_path; /* only when mounted ourselves */
};

enum {
	PROP_0,
	PROP_PARTITION_KIND,
	PROP_MOUNT_PATH,
	PROP_PROXY_BLOCK,
	PROP_PROXY_FILESYSTEM,
	PROP_LAST
};

G_DEFINE_TYPE(FuVolume, fu_volume, G_TYPE_OBJECT)

static void
fu_volume_finalize(GObject *obj)
{
	FuVolume *self = FU_VOLUME(obj);
	g_free(self->partition_kind);
	g_free(self->mount_path);
	if (self->proxy_blk != NULL)
		g_object_unref(self->proxy_blk);
	if (self->proxy_fs != NULL)
		g_object_unref(self->proxy_fs);
	G_OBJECT_CLASS(fu_volume_parent_class)->finalize(obj);
}

static void
fu_volume_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuVolume *self = FU_VOLUME(object);
	switch (prop_id) {
	case PROP_PARTITION_KIND:
		g_value_set_string(value, self->partition_kind);
		break;
	case PROP_MOUNT_PATH:
		g_value_set_string(value, self->mount_path);
		break;
	case PROP_PROXY_BLOCK:
		g_value_set_object(value, self->proxy_blk);
		break;
	case PROP_PROXY_FILESYSTEM:
		g_value_set_object(value, self->proxy_fs);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_volume_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuVolume *self = FU_VOLUME(object);
	switch (prop_id) {
	case PROP_PARTITION_KIND:
		self->partition_kind = g_value_dup_string(value);
		break;
	case PROP_MOUNT_PATH:
		self->mount_path = g_value_dup_string(value);
		break;
	case PROP_PROXY_BLOCK:
		self->proxy_blk = g_value_dup_object(value);
		break;
	case PROP_PROXY_FILESYSTEM:
		self->proxy_fs = g_value_dup_object(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_volume_class_init(FuVolumeClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_volume_finalize;
	object_class->get_property = fu_volume_get_property;
	object_class->set_property = fu_volume_set_property;

	/**
	 * FuVolume:proxy-block:
	 *
	 * The proxy for the block interface.
	 *
	 * Since: 1.4.6
	 */
	pspec =
	    g_param_spec_object("proxy-block",
				NULL,
				NULL,
				G_TYPE_DBUS_PROXY,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PROXY_BLOCK, pspec);

	/**
	 * FuVolume:proxy-filesystem:
	 *
	 * The proxy for the filesystem interface.
	 *
	 * Since: 1.4.6
	 */
	pspec =
	    g_param_spec_object("proxy-filesystem",
				NULL,
				NULL,
				G_TYPE_DBUS_PROXY,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PROXY_FILESYSTEM, pspec);

	/**
	 * FuVolume:mount-path:
	 *
	 * The UNIX mount path.
	 *
	 * Since: 1.4.6
	 */
	pspec =
	    g_param_spec_string("mount-path",
				NULL,
				NULL,
				NULL,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_MOUNT_PATH, pspec);

	/**
	 * FuVolume:partition-kind:
	 *
	 * The partition kind.
	 *
	 * Since: 1.8.13
	 */
	pspec =
	    g_param_spec_string("partition-kind",
				NULL,
				NULL,
				NULL,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PARTITION_KIND, pspec);
}

static void
fu_volume_init(FuVolume *self)
{
}

/**
 * fu_volume_get_id:
 * @self: a @FuVolume
 *
 * Gets the D-Bus path of the mount point.
 *
 * Returns: string ID, or %NULL
 *
 * Since: 1.4.6
 **/
const gchar *
fu_volume_get_id(FuVolume *self)
{
	g_return_val_if_fail(FU_IS_VOLUME(self), NULL);
	if (self->proxy_fs != NULL)
		return g_dbus_proxy_get_object_path(self->proxy_fs);
	if (self->proxy_blk != NULL)
		return g_dbus_proxy_get_object_path(self->proxy_blk);
	return NULL;
}

/**
 * fu_volume_get_partition_kind:
 * @self: a @FuVolume
 *
 * Gets the partition kind of the volume mount point.
 *
 * NOTE: If you want this to be converted to a GPT-style GUID then use
 * fu_volume_kind_convert_to_gpt() on the return value of this function.
 *
 * Returns: partition kind, e.g. `0x06`, `vfat` or a GUID like `FU_VOLUME_KIND_ESP`
 *
 * Since: 1.8.13
 **/
const gchar *
fu_volume_get_partition_kind(FuVolume *self)
{
	g_return_val_if_fail(FU_IS_VOLUME(self), NULL);
	return self->partition_kind;
}

/**
 * fu_volume_get_mount_point:
 * @self: a @FuVolume
 *
 * Gets the location of the volume mount point.
 *
 * Returns: UNIX path, or %NULL
 *
 * Since: 1.4.6
 **/
gchar *
fu_volume_get_mount_point(FuVolume *self)
{
	g_autofree const gchar **mountpoints = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail(FU_IS_VOLUME(self), NULL);

	/* we mounted it */
	if (self->mount_path != NULL)
		return g_strdup(self->mount_path);

	/* something else mounted it */
	if (self->proxy_fs == NULL)
		return NULL;
	val = g_dbus_proxy_get_cached_property(self->proxy_fs, "MountPoints");
	if (val == NULL)
		return NULL;
	mountpoints = g_variant_get_bytestring_array(val, NULL);
	return g_strdup(mountpoints[0]);
}

/**
 * fu_volume_check_free_space:
 * @self: a @FuVolume
 * @required: size in bytes
 * @error: (nullable): optional return location for an error
 *
 * Checks the volume for required space.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fu_volume_check_free_space(FuVolume *self, guint64 required, GError **error)
{
	guint64 fs_free;
	g_autofree gchar *path = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	g_return_val_if_fail(FU_IS_VOLUME(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* skip the checks for unmounted disks */
	path = fu_volume_get_mount_point(self);
	if (path == NULL)
		return TRUE;

	file = g_file_new_for_path(path);
	info = g_file_query_filesystem_info(file, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, error);
	if (info == NULL)
		return FALSE;
	fs_free = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	if (fs_free < required) {
		g_autofree gchar *str_free = g_format_size(fs_free);
		g_autofree gchar *str_reqd = g_format_size(required);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s does not have sufficient space, required %s, got %s",
			    path,
			    str_reqd,
			    str_free);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_volume_is_mounted:
 * @self: a @FuVolume
 *
 * Checks if the VOLUME is already mounted.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fu_volume_is_mounted(FuVolume *self)
{
	g_autofree gchar *mount_point = NULL;
	g_return_val_if_fail(FU_IS_VOLUME(self), FALSE);
	mount_point = fu_volume_get_mount_point(self);
	return mount_point != NULL;
}

/**
 * fu_volume_is_encrypted:
 * @self: a @FuVolume
 *
 * Checks if the VOLUME is currently encrypted.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.1
 **/
gboolean
fu_volume_is_encrypted(FuVolume *self)
{
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail(FU_IS_VOLUME(self), FALSE);

	if (self->proxy_blk == NULL)
		return FALSE;
	val = g_dbus_proxy_get_cached_property(self->proxy_blk, "CryptoBackingDevice");
	if (val == NULL)
		return FALSE;
	if (g_strcmp0(g_variant_get_string(val, NULL), "/") == 0)
		return FALSE;
	return TRUE;
}

/**
 * fu_volume_mount:
 * @self: a @FuVolume
 * @error: (nullable): optional return location for an error
 *
 * Mounts the VOLUME ready for use.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fu_volume_mount(FuVolume *self, GError **error)
{
	GVariantBuilder builder;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail(FU_IS_VOLUME(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* device from the self tests */
	if (self->proxy_fs == NULL)
		return TRUE;

	g_debug("mounting %s", fu_volume_get_id(self));
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	val = g_dbus_proxy_call_sync(self->proxy_fs,
				     "Mount",
				     g_variant_new("(a{sv})", &builder),
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL,
				     &error_local);
	if (val == NULL) {
		if (g_error_matches(error_local, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE) ||
		    g_error_matches(error_local, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD)) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
					    error_local->message);
			return FALSE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	g_variant_get(val, "(s)", &self->mount_path);
	return TRUE;
}

/**
 * fu_volume_is_internal:
 * @self: a @FuVolume
 *
 * Guesses if the drive is internal to the system
 *
 * Returns: %TRUE for success
 *
 * Since: 1.5.2
 **/
gboolean
fu_volume_is_internal(FuVolume *self)
{
	g_autoptr(GVariant) val_system = NULL;
	g_return_val_if_fail(FU_IS_VOLUME(self), FALSE);

	val_system = g_dbus_proxy_get_cached_property(self->proxy_blk, "HintSystem");
	if (val_system == NULL)
		return FALSE;

	return g_variant_get_boolean(val_system);
}

/**
 * fu_volume_get_id_type:
 * @self: a @FuVolume
 *
 * Return the IdType of the volume
 *
 * Returns: string for type or NULL
 *
 * Since: 1.5.2
 **/
gchar *
fu_volume_get_id_type(FuVolume *self)
{
	g_autoptr(GVariant) val = NULL;
	g_return_val_if_fail(FU_IS_VOLUME(self), NULL);

	val = g_dbus_proxy_get_cached_property(self->proxy_blk, "IdType");
	if (val == NULL)
		return NULL;

	return g_strdup(g_variant_get_string(val, NULL));
}

/**
 * fu_volume_unmount:
 * @self: a @FuVolume
 * @error: (nullable): optional return location for an error
 *
 * Unmounts the volume after use.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fu_volume_unmount(FuVolume *self, GError **error)
{
	GVariantBuilder builder;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail(FU_IS_VOLUME(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* device from the self tests */
	if (self->proxy_fs == NULL)
		return TRUE;

	g_debug("unmounting %s", fu_volume_get_id(self));
	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	val = g_dbus_proxy_call_sync(self->proxy_fs,
				     "Unmount",
				     g_variant_new("(a{sv})", &builder),
				     G_DBUS_CALL_FLAGS_NONE,
				     -1,
				     NULL,
				     error);
	if (val == NULL)
		return FALSE;
	g_free(self->mount_path);
	self->mount_path = NULL;
	return TRUE;
}

/**
 * fu_volume_locker:
 * @self: a @FuVolume
 * @error: (nullable): optional return location for an error
 *
 * Locks the volume, mounting it and unmounting it as required. If the volume is
 * already mounted then it is is _not_ unmounted when the locker is closed.
 *
 * Returns: (transfer full): a device locker for success, or %NULL
 *
 * Since: 1.4.6
 **/
FuDeviceLocker *
fu_volume_locker(FuVolume *self, GError **error)
{
	g_return_val_if_fail(FU_IS_VOLUME(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* already open, so NOP */
	if (fu_volume_is_mounted(self))
		return g_object_new(FU_TYPE_DEVICE_LOCKER, NULL);
	return fu_device_locker_new_full(self,
					 (FuDeviceLockerFunc)fu_volume_mount,
					 (FuDeviceLockerFunc)fu_volume_unmount,
					 error);
}

/* private */
FuVolume *
fu_volume_new_from_mount_path(const gchar *mount_path)
{
	g_autoptr(FuVolume) self = g_object_new(FU_TYPE_VOLUME, NULL);
	g_return_val_if_fail(mount_path != NULL, NULL);
	self->mount_path = g_strdup(mount_path);
	return g_steal_pointer(&self);
}

/**
 * fu_volume_kind_convert_to_gpt:
 * @kind: UDisk reported type string, e.g. `efi` or `0xef`
 *
 * Converts a MBR type to a GPT type.
 *
 * Returns: the GPT type, usually a GUID. If not known @kind is returned.
 *
 * Since: 1.8.6
 **/
const gchar *
fu_volume_kind_convert_to_gpt(const gchar *kind)
{
	struct {
		const gchar *gpt;
		const gchar *mbrs[6];
	} typeguids[] = {{"c12a7328-f81f-11d2-ba4b-00a0c93ec93b", /* esp */
			  {"0xef", "efi", NULL}},
			 {"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", /* fat32 */
			  {"0x0b", "0x06", "vfat", "fat32", "fat32lba", NULL}},
			 {NULL, {NULL}}};
	for (guint i = 0; typeguids[i].gpt != NULL; i++) {
		for (guint j = 0; typeguids[i].mbrs[j] != NULL; j++) {
			if (g_strcmp0(kind, typeguids[i].mbrs[j]) == 0)
				return typeguids[i].gpt;
		}
	}
	return kind;
}

/**
 * fu_volume_new_by_kind:
 * @kind: a volume kind, typically a GUID
 * @error: (nullable): optional return location for an error
 *
 * Finds all volumes of a specific partition type
 *
 * Returns: (transfer container) (element-type FuVolume): a #GPtrArray, or %NULL if the kind was not
 *found
 *
 * Since: 1.8.2
 **/
GPtrArray *
fu_volume_new_by_kind(const gchar *kind, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) volumes = NULL;

	g_return_val_if_fail(kind != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	devices = fu_common_get_block_devices(error);
	if (devices == NULL)
		return NULL;
	volumes = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < devices->len; i++) {
		GDBusProxy *proxy_blk = g_ptr_array_index(devices, i);
		const gchar *type_str;
		g_autofree gchar *id_type = NULL;
		g_autoptr(FuVolume) vol = NULL;
		g_autoptr(GDBusProxy) proxy_part = NULL;
		g_autoptr(GDBusProxy) proxy_fs = NULL;
		g_autoptr(GError) error_proxy_fs = NULL;
		g_autoptr(GVariant) val = NULL;

		proxy_part = g_dbus_proxy_new_sync(g_dbus_proxy_get_connection(proxy_blk),
						   G_DBUS_PROXY_FLAGS_NONE,
						   NULL,
						   UDISKS_DBUS_SERVICE,
						   g_dbus_proxy_get_object_path(proxy_blk),
						   UDISKS_DBUS_INTERFACE_PARTITION,
						   NULL,
						   error);
		if (proxy_part == NULL) {
			g_prefix_error(error,
				       "failed to initialize d-bus proxy %s: ",
				       g_dbus_proxy_get_object_path(proxy_blk));
			return NULL;
		}
		val = g_dbus_proxy_get_cached_property(proxy_part, "Type");
		if (val == NULL)
			continue;

		g_variant_get(val, "&s", &type_str);
		proxy_fs = g_dbus_proxy_new_sync(g_dbus_proxy_get_connection(proxy_blk),
						 G_DBUS_PROXY_FLAGS_NONE,
						 NULL,
						 UDISKS_DBUS_SERVICE,
						 g_dbus_proxy_get_object_path(proxy_blk),
						 UDISKS_DBUS_INTERFACE_FILESYSTEM,
						 NULL,
						 &error_proxy_fs);
		if (proxy_fs == NULL) {
			g_debug("failed to get filesystem for %s: %s",
				g_dbus_proxy_get_object_path(proxy_blk),
				error_proxy_fs->message);
			continue;
		}
		vol = g_object_new(FU_TYPE_VOLUME,
				   "partition-kind",
				   type_str,
				   "proxy-block",
				   proxy_blk,
				   "proxy-filesystem",
				   proxy_fs,
				   NULL);

		/* convert reported type to GPT type */
		type_str = fu_volume_kind_convert_to_gpt(type_str);
		id_type = fu_volume_get_id_type(vol);
		g_debug("device %s, type: %s, internal: %d, fs: %s",
			g_dbus_proxy_get_object_path(proxy_blk),
			type_str,
			fu_volume_is_internal(vol),
			id_type);
		if (g_strcmp0(type_str, kind) != 0)
			continue;
		g_ptr_array_add(volumes, g_steal_pointer(&vol));
	}
	if (volumes->len == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no volumes of type %s", kind);
		return NULL;
	}
	return g_steal_pointer(&volumes);
}

/**
 * fu_volume_new_by_device:
 * @device: a device string, typically starting with `/dev/`
 * @error: (nullable): optional return location for an error
 *
 * Finds the first volume from the specified device.
 *
 * Returns: (transfer full): a volume, or %NULL if the device was not found
 *
 * Since: 1.8.2
 **/
FuVolume *
fu_volume_new_by_device(const gchar *device, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(device != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find matching block device */
	devices = fu_common_get_block_devices(error);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		GDBusProxy *proxy_blk = g_ptr_array_index(devices, i);
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy_blk, "Device");
		if (val == NULL)
			continue;
		if (g_strcmp0(g_variant_get_bytestring(val), device) == 0) {
			g_autoptr(GDBusProxy) proxy_fs = NULL;
			g_autoptr(GError) error_local = NULL;
			proxy_fs = g_dbus_proxy_new_sync(g_dbus_proxy_get_connection(proxy_blk),
							 G_DBUS_PROXY_FLAGS_NONE,
							 NULL,
							 UDISKS_DBUS_SERVICE,
							 g_dbus_proxy_get_object_path(proxy_blk),
							 UDISKS_DBUS_INTERFACE_FILESYSTEM,
							 NULL,
							 &error_local);
			if (proxy_fs == NULL)
				g_debug("ignoring: %s", error_local->message);
			return g_object_new(FU_TYPE_VOLUME,
					    "proxy-block",
					    proxy_blk,
					    "proxy-filesystem",
					    proxy_fs,
					    NULL);
		}
	}

	/* failed */
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no volumes for device %s", device);
	return NULL;
}

/**
 * fu_volume_new_by_devnum:
 * @devnum: a device number
 * @error: (nullable): optional return location for an error
 *
 * Finds the first volume from the specified device.
 *
 * Returns: (transfer full): a volume, or %NULL if the device was not found
 *
 * Since: 1.8.2
 **/
FuVolume *
fu_volume_new_by_devnum(guint32 devnum, GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find matching block device */
	devices = fu_common_get_block_devices(error);
	if (devices == NULL)
		return NULL;
	for (guint i = 0; i < devices->len; i++) {
		GDBusProxy *proxy_blk = g_ptr_array_index(devices, i);
		g_autoptr(GVariant) val = NULL;
		val = g_dbus_proxy_get_cached_property(proxy_blk, "DeviceNumber");
		if (val == NULL)
			continue;
		if (devnum == g_variant_get_uint64(val)) {
			return g_object_new(FU_TYPE_VOLUME, "proxy-block", proxy_blk, NULL);
		}
	}

	/* failed */
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no volumes for devnum %u", devnum);
	return NULL;
}

/**
 * fu_volume_new_esp_for_path:
 * @esp_path: a path to the ESP
 * @error: (nullable): optional return location for an error
 *
 * Gets the platform ESP using a UNIX or UDisks path
 *
 * Returns: (transfer full): a #volume, or %NULL if the ESP was not found
 *
 * Since: 1.8.2
 **/
FuVolume *
fu_volume_new_esp_for_path(const gchar *esp_path, GError **error)
{
	g_autofree gchar *basename = NULL;
	g_autoptr(GPtrArray) volumes = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(esp_path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	volumes = fu_volume_new_by_kind(FU_VOLUME_KIND_ESP, &error_local);
	if (volumes == NULL) {
		/* check if it's a valid directory already */
		if (g_file_test(esp_path, G_FILE_TEST_IS_DIR))
			return fu_volume_new_from_mount_path(esp_path);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return NULL;
	}
	basename = g_path_get_basename(esp_path);
	for (guint i = 0; i < volumes->len; i++) {
		FuVolume *vol = g_ptr_array_index(volumes, i);
		const gchar *mount_point = fu_volume_get_mount_point(vol);
		g_autofree gchar *vol_basename = NULL;
		if (mount_point == NULL)
			continue;
		vol_basename = g_path_get_basename(mount_point);
		if (g_strcmp0(basename, vol_basename) == 0)
			return g_object_ref(vol);
	}
	if (g_file_test(esp_path, G_FILE_TEST_IS_DIR)) {
		g_info("using user requested path %s for ESP", esp_path);
		return fu_volume_new_from_mount_path(esp_path);
	}
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_INVALID_FILENAME,
		    "No ESP with path %s",
		    esp_path);
	return NULL;
}
