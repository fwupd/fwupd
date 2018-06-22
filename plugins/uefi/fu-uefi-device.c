/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-uefi-common.h"
#include "fu-uefi-device.h"

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
};

G_DEFINE_TYPE (FuUefiDevice, fu_uefi_device, FU_TYPE_DEVICE)

const gchar *
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

static void
fu_uefi_device_init (FuUefiDevice *self)
{
}

static void
fu_uefi_device_finalize (GObject *object)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (object);

	g_free (self->fw_class);

	G_OBJECT_CLASS (fu_uefi_device_parent_class)->finalize (object);
}

static void
fu_uefi_device_class_init (FuUefiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_uefi_device_finalize;
	klass_device->to_string = fu_uefi_device_to_string;
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

