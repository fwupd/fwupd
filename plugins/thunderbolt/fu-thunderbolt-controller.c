/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-thunderbolt-common.h"
#include "fu-thunderbolt-controller.h"
#include "fu-thunderbolt-firmware.h"

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
		else
			return "Thunderbolt host controller";
	}
	if (self->controller_kind == FU_THUNDERBOLT_CONTROLLER_KIND_DEVICE) {
		if (self->gen >= 4)
			return "USB4 device controller";
		else
			return "Thunderbolt device controller";
	}
	return "Unknown";
}

static void
fu_thunderbolt_controller_to_string(FuDevice *device, guint idt, GString *str)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(device);

	/* FuThunderboltDevice->to_string */
	FU_DEVICE_CLASS(fu_thunderbolt_controller_parent_class)->to_string(device, idt, str);

	fu_string_append(str, idt, "Device Type", fu_thunderbolt_controller_kind_to_string(self));
	fu_string_append_kb(str, idt, "Safe Mode", self->safe_mode);
	fu_string_append_kb(str, idt, "Native mode", self->is_native);
	fu_string_append_ku(str, idt, "Generation", self->gen);
}

static gboolean
fu_thunderbolt_controller_probe(FuDevice *device, GError **error)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(device);
	const gchar *unique_id;
	g_autofree gchar *parent_name = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_thunderbolt_controller_parent_class)->probe(device, error))
		return FALSE;

	/* determine if host controller or not */
	parent_name = fu_udev_device_get_parent_name(FU_UDEV_DEVICE(self));
	if (parent_name != NULL && g_str_has_prefix(parent_name, "domain"))
		self->controller_kind = FU_THUNDERBOLT_CONTROLLER_KIND_HOST;
	unique_id = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "unique_id", NULL);
	if (unique_id != NULL)
		fu_device_set_physical_id(device, unique_id);

	/* success */
	return TRUE;
}

static gboolean
fu_thunderbolt_controller_read_status_block(FuThunderboltController *self, GError **error)
{
	gsize nr_chunks;
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GBytes) controller_fw = NULL;
	g_autoptr(GInputStream) istr = NULL;
	g_autoptr(FuThunderboltFirmware) firmware = fu_thunderbolt_firmware_new();

	nvmem = fu_thunderbolt_device_find_nvmem(FU_THUNDERBOLT_DEVICE(self), TRUE, error);
	if (nvmem == NULL)
		return FALSE;

	/* read just enough bytes to read the status byte */
	nr_chunks = (FU_TBT_OFFSET_NATIVE + FU_TBT_CHUNK_SZ - 1) / FU_TBT_CHUNK_SZ;
	istr = G_INPUT_STREAM(g_file_read(nvmem, NULL, error));
	if (istr == NULL)
		return FALSE;
	controller_fw = g_input_stream_read_bytes(istr, nr_chunks * FU_TBT_CHUNK_SZ, NULL, error);
	if (controller_fw == NULL)
		return FALSE;
	if (!fu_firmware_parse(FU_FIRMWARE(firmware),
			       controller_fw,
			       FWUPD_INSTALL_FLAG_NO_SEARCH,
			       error))
		return FALSE;
	self->is_native = fu_thunderbolt_firmware_is_native(firmware);
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
	self->host_online_timer_id =
	    g_timeout_add_seconds(5, fu_thunderbolt_controller_set_port_online_cb, self);
	return TRUE;
}

static void
fu_thunderbolt_controller_set_signed(FuDevice *device)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(device);
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	const gchar *tmp;

	/* if it's a USB4 type not of host and generation 3; it's Intel */
	tmp = g_udev_device_get_property(udev_device, "USB4_TYPE");
	if (g_strcmp0(tmp, "host") != 0 && self->gen == 3)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
}

static gboolean
fu_thunderbolt_controller_setup(FuDevice *device, GError **error)
{
	FuThunderboltController *self = FU_THUNDERBOLT_CONTROLLER(device);
	const gchar *tmp = NULL;
	guint16 did;
	guint16 vid;
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
	vid = fu_udev_device_get_vendor(FU_UDEV_DEVICE(self));
	if (vid == 0x0)
		g_debug("failed to get Vendor ID");

	did = fu_udev_device_get_model(FU_UDEV_DEVICE(self));
	if (did == 0x0)
		g_debug("failed to get Device ID");

	/* requires kernel 5.5 or later, non-fatal if not available */
	self->gen =
	    fu_thunderbolt_udev_get_attr_uint16(FU_UDEV_DEVICE(self), "generation", &error_gen);
	if (self->gen == 0)
		g_debug("Unable to read generation: %s", error_gen->message);

	if (self->controller_kind == FU_THUNDERBOLT_CONTROLLER_KIND_HOST) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_set_summary(device, "Unmatched performance for high-speed I/O");
	} else {
		tmp = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(self), "device_name", NULL);
	}

	/* set the controller name */
	if (tmp == NULL)
		tmp = fu_thunderbolt_controller_kind_to_string(self);
	fu_device_set_name(device, tmp);

	/* set vendor string */
	tmp = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(self), "vendor_name", NULL);
	if (tmp != NULL)
		fu_device_set_vendor(device, tmp);

	if (fu_device_get_version(device) == NULL)
		fu_thunderbolt_controller_check_safe_mode(self);

	if (self->safe_mode) {
		fu_device_set_update_error(device, "Device is in safe mode");
	} else {
		g_autofree gchar *device_id = NULL;
		g_autofree gchar *domain_id = NULL;
		if (fu_thunderbolt_controller_can_update(self)) {
			const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
			g_autofree gchar *vendor_id = NULL;
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
			vendor_id = g_strdup_printf("TBT:0x%04X", (guint)vid);
			fu_device_add_vendor_id(device, vendor_id);
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
			device_id = g_strdup("TBT-fixed");
		}
		fu_device_add_instance_id(device, device_id);
		if (domain_id != NULL)
			fu_device_add_instance_id(device, domain_id);
	}

	/* determine if we can update on unplug */
	if (fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device),
					  "nvm_authenticate_on_disconnect",
					  NULL) != NULL) {
		fu_thunderbolt_device_set_auth_method(FU_THUNDERBOLT_DEVICE(self),
						      "nvm_authenticate_on_disconnect");
		/* flushes image */
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
		/* forces the device to write to authenticate on disconnect attribute */
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
		/* control the order of activation (less relevant; install too though) */
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST);
	} else {
		fu_device_add_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_thunderbolt_controller_finalize;
	klass_device->setup = fu_thunderbolt_controller_setup;
	klass_device->probe = fu_thunderbolt_controller_probe;
	klass_device->to_string = fu_thunderbolt_controller_to_string;
	klass_device->write_firmware = fu_thunderbolt_controller_write_firmware;
}
