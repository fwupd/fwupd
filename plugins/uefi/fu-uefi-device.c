/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-device-metadata.h"

#include "fu-uefi-bgrt.h"
#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-device-info.h"
#include "fu-uefi-bootmgr.h"

struct _FuUefiDevice {
	FuDevice		 parent_instance;
	gchar			*fw_class;
	FuUefiDeviceKind	 kind;
	guint32			 capsule_flags;
	guint32			 fw_version;
	guint32			 fw_version_lowest;
	FuUefiDeviceStatus	 last_attempt_status;
	guint32			 last_attempt_version;
	guint64			 fmp_hardware_instance;
	FuUefiDeviceInfo	*info;
};

G_DEFINE_TYPE (FuUefiDevice, fu_uefi_device, FU_TYPE_DEVICE)

static const gchar *
fu_uefi_device_kind_to_string (FuUefiDeviceKind kind)
{
	if (kind == FU_UEFI_DEVICE_KIND_UNKNOWN)
		return "unknown";
	if (kind == FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE)
		return "system-firmware";
	if (kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE)
		return "device-firmware";
	if (kind == FU_UEFI_DEVICE_KIND_UEFI_DRIVER)
		return "uefi-driver";
	if (kind == FU_UEFI_DEVICE_KIND_FMP)
		return "fmp";
	return NULL;
}

static FuUefiDeviceKind
fu_uefi_device_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "system-firmware") == 0)
		return FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE;
	if (g_strcmp0 (kind, "device-firmware") == 0)
		return FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE;
	if (g_strcmp0 (kind, "uefi-driver") == 0)
		return FU_UEFI_DEVICE_KIND_UEFI_DRIVER;
	if (g_strcmp0 (kind, "fmp") == 0)
		return FU_UEFI_DEVICE_KIND_FMP;
	return FU_UEFI_DEVICE_KIND_UNKNOWN;
}

const gchar *
fu_uefi_device_status_to_string (FuUefiDeviceStatus status)
{
	if (status == FU_UEFI_DEVICE_STATUS_SUCCESS)
		return "success";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_UNSUCCESSFUL)
		return "unsuccessful";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_INSUFFICIENT_RESOURCES)
		return "insufficient resources";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_INCORRECT_VERSION)
		return "incorrect version";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_INVALID_FORMAT)
		return "invalid firmware format";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_AUTH_ERROR)
		return "authentication signing error";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_AC)
		return "AC power required";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_BATT)
		return "battery level is too low";
	return NULL;
}

static void
fu_uefi_device_to_string (FuDevice *device, GString *str)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (device);
	g_string_append (str, "  FuUefiDevice:\n");
	g_string_append_printf (str, "    kind:\t\t\t%s\n",
				fu_uefi_device_kind_to_string (self->kind));
	g_string_append_printf (str, "    fw_class:\t\t\t%s\n", self->fw_class);
	g_string_append_printf (str, "    capsule_flags:\t\t%" G_GUINT32_FORMAT "\n",
				self->capsule_flags);
	g_string_append_printf (str, "    fw_version:\t\t\t%" G_GUINT32_FORMAT "\n",
				self->fw_version);
	g_string_append_printf (str, "    fw_version_lowest:\t\t%" G_GUINT32_FORMAT "\n",
				self->fw_version_lowest);
	g_string_append_printf (str, "    last_attempt_status:\t%s\n",
				fu_uefi_device_status_to_string (self->last_attempt_status));
	g_string_append_printf (str, "    last_attempt_version:\t%" G_GUINT32_FORMAT "\n",
				self->last_attempt_version);
}

FuUefiDeviceKind
fu_uefi_device_get_kind (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0);
	return self->kind;
}

guint32
fu_uefi_device_get_version (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->fw_version;
}

guint32
fu_uefi_device_get_version_lowest (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->fw_version_lowest;
}

guint32
fu_uefi_device_get_version_error (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->last_attempt_version;
}

guint64
fu_uefi_device_get_hardware_instance (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->fmp_hardware_instance;
}

FuUefiDeviceStatus
fu_uefi_device_get_status (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0);
	return self->last_attempt_status;
}

guint32
fu_uefi_device_get_capsule_flags (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->capsule_flags;
}

const gchar *
fu_uefi_device_get_guid (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), NULL);
	return self->fw_class;
}

static gboolean
fu_uefi_device_ensure_info (FuUefiDevice *self, GError **error)
{
	if (self->fw_class == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot ensure device info with no GUID");
		return FALSE;
	}
	if (self->info != NULL)
		return TRUE;
	self->info = fu_uefi_device_info_new (self->fw_class, 0, error);
	if (self->info == NULL)
		return FALSE;
	return TRUE;
}

gboolean
fu_uefi_device_clear_status (FuUefiDevice *self, GError **error)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!fu_uefi_device_ensure_info (self, error))
		return FALSE;
	self->info->status = FU_UEFI_DEVICE_STATUS_SUCCESS;
	return fu_uefi_device_info_update (self->info, error);
}

static gboolean
fu_uefi_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (device);
	const gchar *esp_path = fu_device_get_metadata (device, "EspPath");
	g_autofree gchar *fn = NULL;

	/* in the self tests */
	if (fu_device_get_metadata (device, "UEFI::FakeESP") != NULL)
		return TRUE;

	/* ensure we have the existing state */
	if (!fu_uefi_device_ensure_info (self, error))
		return FALSE;

	/* save the blob to either a filename that we've used before for this
	 * GUID or construct something sane */
	fn = fu_uefi_device_info_get_media_path (esp_path, self->info);
	if (!fu_common_mkdir_parent (fn, error))
		return FALSE;
	if (!fu_common_set_contents_bytes (fn, fw, error))
		return FALSE;

	/* self tests */
	if (fu_device_get_metadata (device, "UEFI::FakeESP") != NULL)
		return TRUE;

	/* set efidp header */
	if (!fu_uefi_device_info_set_device_path (self->info, fn, error))
		return FALSE;

	/* save this to the hardware */
	self->info->status = FWUPDATE_ATTEMPT_UPDATE;
	self->info->capsule_flags = self->capsule_flags;
	memset (&self->info->time_attempted, 0x0, sizeof(self->info->time_attempted));
	if (!fu_uefi_device_info_update (self->info, error)) {
		fu_uefi_prefix_efi_errors (error);
		return FALSE;
	}

	/* update the firmware before the bootloader runs */
	if (!fu_uefi_setup_bootnext (esp_path, error))
		return FALSE;

	/* success! */
	return TRUE;
}

static void
fu_uefi_device_init (FuUefiDevice *self)
{
}

static void
fu_uefi_device_finalize (GObject *object)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (object);

	g_free (self->fw_class);
	fu_uefi_device_info_free (self->info);

	G_OBJECT_CLASS (fu_uefi_device_parent_class)->finalize (object);
}

static void
fu_uefi_device_class_init (FuUefiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_uefi_device_finalize;
	klass_device->to_string = fu_uefi_device_to_string;
	klass_device->write_firmware = fu_uefi_device_write_firmware;
}

FuUefiDevice *
fu_uefi_device_new_from_entry (const gchar *entry_path)
{
	FuUefiDevice *self;
	g_autofree gchar *fw_class_fn = NULL;
	g_autofree gchar *id = NULL;

	g_return_val_if_fail (entry_path != NULL, FALSE);

	/* create object */
	self = g_object_new (FU_TYPE_UEFI_DEVICE, NULL);

	/* read values from sysfs */
	fw_class_fn = g_build_filename (entry_path, "fw_class", NULL);
	if (g_file_get_contents (fw_class_fn, &self->fw_class, NULL, NULL)) {
		g_strdelimit (self->fw_class, "\n", '\0');
		fu_device_add_guid (FU_DEVICE (self), self->fw_class);
	}
	self->capsule_flags = fu_uefi_read_file_as_uint64 (entry_path, "capsule_flags");
	self->kind = fu_uefi_read_file_as_uint64 (entry_path, "fw_type");
	self->fw_version = fu_uefi_read_file_as_uint64 (entry_path, "fw_version");
	self->last_attempt_status = fu_uefi_read_file_as_uint64 (entry_path, "last_attempt_status");
	self->last_attempt_version = fu_uefi_read_file_as_uint64 (entry_path, "last_attempt_version");
	self->fw_version_lowest = fu_uefi_read_file_as_uint64 (entry_path, "lowest_supported_fw_version");
	g_assert (self->fw_class != NULL);

	/* the hardware instance is not in the ESRT table and we should really
	 * write the EFI stub to query with FMP -- but we still have not ever
	 * seen a PCIe device with FMP support... */
	self->fmp_hardware_instance = 0x0;

	/* set ID */
	id = g_strdup_printf ("UEFI-%s-dev%" G_GUINT64_FORMAT,
			      self->fw_class, self->fmp_hardware_instance);
	fu_device_set_id (FU_DEVICE (self), id);

	return self;
}

FuUefiDevice *
fu_uefi_device_new_from_dev (FuDevice *dev)
{
	const gchar *tmp;
	FuUefiDevice *self;

	g_return_val_if_fail (fu_device_get_guid_default (dev) != NULL, NULL);

	/* create virtual object not backed by an ESRT entry */
	self = g_object_new (FU_TYPE_UEFI_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), dev);
	self->fw_class = g_strdup (fu_device_get_guid_default (dev));
	tmp = fu_device_get_metadata (dev, FU_DEVICE_METADATA_UEFI_DEVICE_KIND);
	self->kind = fu_uefi_device_kind_from_string (tmp);
	self->capsule_flags = 0; /* FIXME? */
	self->fw_version = 0; /* FIXME? */
	g_assert (self->fw_class != NULL);
	return self;
}
