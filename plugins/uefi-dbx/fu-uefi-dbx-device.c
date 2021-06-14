/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-uefi-dbx-common.h"
#include "fu-uefi-dbx-device.h"

struct _FuUefiDbxDevice {
	FuDevice		 parent_instance;
};

G_DEFINE_TYPE (FuUefiDbxDevice, fu_uefi_dbx_device, FU_TYPE_DEVICE)

static gboolean
fu_uefi_dbx_device_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags install_flags,
				   GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* write entire chunk to efivarfs */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	buf = g_bytes_get_data (fw, &bufsz);
	if (!fu_efivar_set_data (FU_EFIVAR_GUID_SECURITY_DATABASE,
				 "dbx", buf, bufsz,
				 FU_EFIVAR_ATTR_APPEND_WRITE |
				 FU_EFIVAR_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS |
				 FU_EFIVAR_ATTR_RUNTIME_ACCESS |
				 FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
				 FU_EFIVAR_ATTR_NON_VOLATILE,
				 error)) {
		return FALSE;
	}

	/* success! */
	return TRUE;
}

static gboolean
fu_uefi_dbx_device_set_version_number (FuDevice *device, GError **error)
{
	g_autoptr(GBytes) dbx_blob = NULL;
	g_autoptr(FuFirmware) dbx = fu_efi_signature_list_new ();

	/* use the number of checksums in the dbx as a version number, ignoring
	 * some owners that do not make sense */
	dbx_blob = fu_efivar_get_data_bytes (FU_EFIVAR_GUID_SECURITY_DATABASE, "dbx", NULL, error);
	if (dbx_blob == NULL)
		return FALSE;
	if (!fu_firmware_parse (dbx, dbx_blob, FWUPD_INSTALL_FLAG_NO_SEARCH, error))
		return FALSE;
	fu_device_set_version (device, fu_firmware_get_version (dbx));
	fu_device_set_version_lowest (device, fu_firmware_get_version (dbx));
	return TRUE;
}

static FuFirmware *
fu_uefi_dbx_prepare_firmware (FuDevice *device,
			      GBytes *fw,
			      FwupdInstallFlags flags,
			      GError **error)
{
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new ();

	/* parse dbx */
	if (!fu_firmware_parse (siglist, fw, flags, error))
		return NULL;

	/* validate this is safe to apply */
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
		if (!fu_uefi_dbx_signature_list_validate (FU_EFI_SIGNATURE_LIST (siglist), error)) {
			g_prefix_error (error,
					"Blocked executable in the ESP, "
					"ensure grub and shim are up to date: ");
			return NULL;
		}
	}

	/* default blob */
	return fu_firmware_new_from_bytes (fw);
}

static gboolean
fu_uefi_dbx_device_probe (FuDevice *device, GError **error)
{
	g_autofree gchar *arch_up = NULL;
	g_autoptr(FuFirmware) kek = fu_efi_signature_list_new ();
	g_autoptr(GBytes) kek_blob = NULL;
	g_autoptr(GPtrArray) sigs = NULL;

	/* use each of the certificates in the KEK to generate the GUIDs */
	kek_blob = fu_efivar_get_data_bytes (FU_EFIVAR_GUID_EFI_GLOBAL, "KEK", NULL, error);
	if (kek_blob == NULL)
		return FALSE;
	if (!fu_firmware_parse (kek, kek_blob, FWUPD_INSTALL_FLAG_NO_SEARCH, error))
		return FALSE;
	arch_up = g_utf8_strup (EFI_MACHINE_TYPE_NAME, -1);
	sigs = fu_firmware_get_images (kek);
	for (guint j = 0; j < sigs->len; j++) {
		FuEfiSignature *sig = g_ptr_array_index (sigs, j);
		g_autofree gchar *checksum = NULL;
		g_autofree gchar *checksum_up = NULL;
		g_autofree gchar *devid1 = NULL;
		g_autofree gchar *devid2 = NULL;

		checksum = fu_firmware_get_checksum (FU_FIRMWARE (sig),
						     G_CHECKSUM_SHA256,
						     error);
		if (checksum == NULL)
			return FALSE;
		checksum_up = g_utf8_strup (checksum, -1);
		devid1 = g_strdup_printf ("UEFI\\CRT_%s", checksum_up);
		fu_device_add_instance_id (device, devid1);
		devid2 = g_strdup_printf ("UEFI\\CRT_%s&ARCH_%s",
					  checksum_up, arch_up);
		fu_device_add_instance_id (device, devid2);
	}
	return fu_uefi_dbx_device_set_version_number (device, error);
}

static void
fu_uefi_dbx_device_init (FuUefiDbxDevice *self)
{
	fu_device_set_physical_id (FU_DEVICE (self), "dbx");
	fu_device_set_name (FU_DEVICE (self), "UEFI dbx");
	fu_device_set_summary (FU_DEVICE (self), "UEFI Revocation Database");
	fu_device_add_vendor_id (FU_DEVICE (self), "UEFI:Linux Foundation");
	fu_device_add_protocol (FU_DEVICE (self), "org.uefi.dbx");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_install_duration (FU_DEVICE (self), 1);
	fu_device_add_icon (FU_DEVICE (self), "computer");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_parent_guid (FU_DEVICE (self), "main-system-firmware");
	if (!fu_common_is_live_media ())
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_uefi_dbx_device_class_init (FuUefiDbxDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->probe = fu_uefi_dbx_device_probe;
	klass_device->write_firmware = fu_uefi_dbx_device_write_firmware;
	klass_device->prepare_firmware = fu_uefi_dbx_prepare_firmware;
}

FuUefiDbxDevice *
fu_uefi_dbx_device_new (void)
{
	FuUefiDbxDevice *self;
	self = g_object_new (FU_TYPE_UEFI_DBX_DEVICE, NULL);
	return self;
}
