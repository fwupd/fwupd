/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-thunderbolt-common.h"
#include "fu-thunderbolt-controller.h"

typedef enum {
	FU_THUNDERBOLT_CONTROLLER_KIND_DEVICE,
	FU_THUNDERBOLT_CONTROLLER_KIND_HOST,
} FuThunderboltControllerKind;

struct _FuThunderboltController {
	FuThunderboltDevice parent_instance;
	FuThunderboltControllerKind controller_kind;
	gboolean safe_mode;
	gboolean is_native;
	guint16 gen;
	guint host_online_timer_id;
};

G_DEFINE_TYPE(FuThunderboltController, fu_thunderbolt_controller, FU_TYPE_THUNDERBOLT_DEVICE)

/* byte offsets in firmware image */
#define FU_TBT_OFFSET_NATIVE 0x7B
#define FU_TBT_CHUNK_SZ	     0x40

static void
fu_thunderbolt_controller_check_safe_mode(FuThunderboltController *self)
{
	const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
	/* failed to read, for host check for safe mode */
	if (self->controller_kind != FU_THUNDERBOLT_CONTROLLER_KIND_DEVICE)
		return;
	g_warning("%s is in safe mode --  VID/DID will "
		  "need to be set by another plugin",
		  devpath);
	self->safe_mode = TRUE;
	fu_device_set_version(FU_DEVICE(self), "00.00");
	fu_device_add_instance_id(FU_DEVICE(self), "TBT-safemode");
	fu_device_set_metadata_boolean(FU_DEVICE(self), FU_DEVICE_METADATA_TBT_IS_SAFE_MODE, TRUE);
}

static const gchar *
fu_thunderbolt_controller_kind_to_string(FuThunderboltController *self)
{
	if (self->controller_kind == FU_THUNDERBOLT_CONTROLLER_KIND_HOST) {
		if (self->gen >= 4)
			return "USB4 host controller";
		return "Thunderbolt host controller";
	}
	if (self->controller_kind == FU_THUNDERBOLT_CONTROLLER_KIND_DEVICE) {
		if (self->gen >= 4)
			return "USB4 device controller";
		return "Thunderbolt device controller";
	}
	return "Unknown";
}

static void
fu_thunderbolt_controller_to_string(FuDevice *device, guint idt, GString *str)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(device);
	fwupd_codec_string_append(str,
				  idt,
				  "DeviceType",
				  fu_thunderbolt_controller_kind_to_string(self));
	fwupd_codec_string_append_bool(str, idt, "SafeMode", self->safe_mode);
	fwupd_codec_string_append_bool(str, idt, "NativeMode", self->is_native);
	fwupd_codec_string_append_int(str, idt, "Generation", self->gen);
}

static gboolean
fu_thunderbolt_controller_probe(FuDevice *device, GError **error)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(device);
	g_autofree gchar *attr_unique_id = NULL;
	g_autoptr(FuDevice) device_parent = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_thunderbolt_controller_parent_class)->probe(device, error))
		return FALSE;

	/* determine if host controller or not */
	device_parent =
	    fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self),
							"thunderbolt:thunderbolt_domain",
							NULL);
	if (device_parent != NULL) {
		g_autofree gchar *parent_name = g_path_get_basename(
		    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device_parent)));
		if (g_str_has_prefix(parent_name, "domain"))
			self->controller_kind = FU_THUNDERBOLT_CONTROLLER_KIND_HOST;
	}

	attr_unique_id = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
						   "unique_id",
						   FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						   NULL);
	if (attr_unique_id != NULL)
		fu_device_set_physical_id(device, attr_unique_id);

	/* success */
	return TRUE;
}

static gboolean
fu_thunderbolt_controller_read_status_block(FuThunderboltController *self, GError **error)
{
	gsize nr_chunks;
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GInputStream) istr = NULL;
	g_autoptr(GInputStream) istr_partial = NULL;
	g_autoptr(FuFirmware) firmware = NULL;

	nvmem = fu_thunderbolt_device_find_nvmem(FU_THUNDERBOLT_DEVICE(self), TRUE, error);
	if (nvmem == NULL)
		return FALSE;

	/* read just enough bytes to read the status byte */
	nr_chunks = (FU_TBT_OFFSET_NATIVE + FU_TBT_CHUNK_SZ - 1) / FU_TBT_CHUNK_SZ;
	istr = G_INPUT_STREAM(g_file_read(nvmem, NULL, error));
	if (istr == NULL)
		return FALSE;
	istr_partial = fu_partial_input_stream_new(istr, 0, nr_chunks * FU_TBT_CHUNK_SZ, error);
	if (istr_partial == NULL)
		return FALSE;
	firmware = fu_firmware_new_from_gtypes(istr_partial,
					       0x0,
					       FWUPD_INSTALL_FLAG_NO_SEARCH,
					       error,
					       FU_TYPE_INTEL_THUNDERBOLT_NVM,
					       FU_TYPE_FIRMWARE,
					       G_TYPE_INVALID);
	if (firmware == NULL)
		return FALSE;
	if (FU_IS_INTEL_THUNDERBOLT_NVM(firmware)) {
		self->is_native =
		    fu_intel_thunderbolt_nvm_is_native(FU_INTEL_THUNDERBOLT_NVM(firmware));
	}
	return TRUE;
}

static gboolean
fu_thunderbolt_controller_can_update(FuThunderboltController *self)
{
	g_autoptr(GError) nvmem_error = NULL;
	g_autoptr(GFile) non_active_nvmem = NULL;

	non_active_nvmem =
	    fu_thunderbolt_device_find_nvmem(FU_THUNDERBOLT_DEVICE(self), FALSE, &nvmem_error);
	if (non_active_nvmem == NULL) {
		g_debug("%s", nvmem_error->message);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_thunderbolt_controller_set_port_online_cb(gpointer user_data)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(user_data);
	g_autoptr(GError) error_local = NULL;

	if (!fu_thunderbolt_udev_set_port_online(FU_UDEV_DEVICE(self), &error_local))
		g_warning("failed to set online after initial delay: %s", error_local->message);

	/* no longer valid */
	self->host_online_timer_id = 0;
	return G_SOURCE_REMOVE;
}

static gboolean
fu_thunderbolt_controller_setup_usb4(FuThunderboltController *self, GError **error)
{
	if (!fu_thunderbolt_udev_set_port_offline(FU_UDEV_DEVICE(self), error))
		return FALSE;
	if (self->host_online_timer_id > 0)
		g_source_remove(self->host_online_timer_id);
	self->host_online_timer_id =
	    g_timeout_add_seconds(5, fu_thunderbolt_controller_set_port_online_cb, self);
	return TRUE;
}

static void
fu_thunderbolt_controller_set_signed(FuDevice *device)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(device);
	g_autofree gchar *prop_type = NULL;

	/* if it's a USB4 type not of host and generation 3; it's Intel */
	prop_type = fu_udev_device_read_property(FU_UDEV_DEVICE(self), "USB4_TYPE", NULL);
	if (g_strcmp0(prop_type, "host") != 0 && self->gen == 3)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
}

static gboolean
fu_thunderbolt_controller_setup(FuDevice *device, GError **error)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(device);
	guint16 did;
	guint16 vid;
	g_autofree gchar *attr_device_name = NULL;
	g_autofree gchar *attr_nvm_authenticate_on_disconnect = NULL;
	g_autofree gchar *attr_vendor_name = NULL;
	g_autoptr(GError) error_gen = NULL;
	g_autoptr(GError) error_version = NULL;

	/* try to read the version */
	if (!fu_thunderbolt_device_get_version(FU_THUNDERBOLT_DEVICE(self), &error_version)) {
		if (self->controller_kind != FU_THUNDERBOLT_CONTROLLER_KIND_HOST &&
		    g_error_matches(error_version, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_propagate_error(error, g_steal_pointer(&error_version));
			return FALSE;
		}
		g_debug("%s", error_version->message);
	}

	/* these may be missing on ICL or later */
	vid = fu_device_get_vid(device);
	if (vid == 0x0)
		g_debug("failed to get Vendor ID");

	did = fu_device_get_pid(device);
	if (did == 0x0)
		g_debug("failed to get Device ID");

	/* requires kernel 5.5 or later, non-fatal if not available */
	self->gen =
	    fu_thunderbolt_udev_get_attr_uint16(FU_UDEV_DEVICE(self), "generation", &error_gen);
	if (self->gen == 0)
		g_debug("unable to read generation: %s", error_gen->message);

	if (self->controller_kind == FU_THUNDERBOLT_CONTROLLER_KIND_HOST) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_set_summary(device, "Unmatched performance for high-speed I/O");
	} else {
		attr_device_name =
		    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					      "device_name",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
	}

	/* set the controller name */
	if (attr_device_name == NULL)
		attr_device_name = g_strdup(fu_thunderbolt_controller_kind_to_string(self));
	fu_device_set_name(device, attr_device_name);

	/* set vendor string */
	attr_vendor_name = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						     "vendor_name",
						     FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						     NULL);
	if (attr_vendor_name != NULL)
		fu_device_set_vendor(device, attr_vendor_name);

	if (fu_device_get_version(device) == NULL)
		fu_thunderbolt_controller_check_safe_mode(self);

	if (self->safe_mode) {
		fu_device_set_update_error(device, "Device is in safe mode");
	} else {
		g_autofree gchar *device_id = NULL;
		g_autofree gchar *domain_id = NULL;
		if (fu_thunderbolt_controller_can_update(self)) {
			const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
			g_autofree gchar *domain = g_path_get_basename(devpath);
			/* USB4 controllers don't have a concept of legacy vs native
			 * so don't try to read a native attribute from their NVM */
			if (self->controller_kind == FU_THUNDERBOLT_CONTROLLER_KIND_HOST &&
			    self->gen < 4) {
				/* read first block of firmware to get the is-native attribute */
				if (!fu_thunderbolt_controller_read_status_block(self, error))
					return FALSE;
			} else {
				self->is_native = FALSE;
			}
			domain_id = g_strdup_printf("TBT-%04x%04x%s-controller%s",
						    (guint)vid,
						    (guint)did,
						    self->is_native ? "-native" : "",
						    domain);
			fu_device_build_vendor_id_u16(device, "TBT", vid);
			device_id = g_strdup_printf("TBT-%04x%04x%s",
						    (guint)vid,
						    (guint)did,
						    self->is_native ? "-native" : "");
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

			/* check if device is authorized */
			if (!fu_thunderbolt_device_check_authorized(FU_THUNDERBOLT_DEVICE(self),
								    error))
				return FALSE;

		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "updates are distributed as part of the platform");
			return FALSE;
		}
		fu_device_add_instance_id(device, device_id);
		if (domain_id != NULL)
			fu_device_add_instance_id(device, domain_id);
	}

	/* determine if we can update on unplug */
	attr_nvm_authenticate_on_disconnect =
	    fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
				      "nvm_authenticate_on_disconnect",
				      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
				      NULL);
	if (attr_nvm_authenticate_on_disconnect != NULL) {
		fu_thunderbolt_device_set_auth_method(FU_THUNDERBOLT_DEVICE(self),
						      "nvm_authenticate_on_disconnect");
		/* flushes image */
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
		/* forces the device to write to authenticate on disconnect attribute */
		fu_device_remove_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART);
		/* control the order of activation (less relevant; install too though) */
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST);
	} else {
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	}
	if (self->controller_kind == FU_THUNDERBOLT_CONTROLLER_KIND_HOST &&
	    fu_device_has_private_flag(FU_DEVICE(self),
				       FU_THUNDERBOLT_DEVICE_FLAG_FORCE_ENUMERATION)) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_thunderbolt_controller_setup_usb4(self, &error_local))
			g_warning("failed to setup host: %s", error_local->message);
	}

	/* set up signed payload attribute */
	fu_thunderbolt_controller_set_signed(device);

	/* success */
	return TRUE;
}

static gboolean
fu_thunderbolt_controller_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	/* FuThunderboltDevice->write_firmware */
	if (!FU_DEVICE_CLASS(fu_thunderbolt_controller_parent_class)
		 ->write_firmware(device, firmware, progress, flags, error))
		return FALSE;
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE))
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_thunderbolt_controller_init(FuThunderboltController *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_THUNDERBOLT_DEVICE_FLAG_FORCE_ENUMERATION);
}

static void
fu_thunderbolt_controller_finalize(GObject *object)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(object);

	if (self->host_online_timer_id != 0)
		g_source_remove(self->host_online_timer_id);

	G_OBJECT_CLASS(fu_thunderbolt_controller_parent_class)->finalize(object);
}

static void
fu_thunderbolt_controller_class_init(FuThunderboltControllerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_thunderbolt_controller_finalize;
	device_class->setup = fu_thunderbolt_controller_setup;
	device_class->probe = fu_thunderbolt_controller_probe;
	device_class->to_string = fu_thunderbolt_controller_to_string;
	device_class->write_firmware = fu_thunderbolt_controller_write_firmware;
}
