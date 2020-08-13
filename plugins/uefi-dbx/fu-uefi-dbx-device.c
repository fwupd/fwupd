/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efivar.h"

#include "fu-efi-signature-common.h"
#include "fu-efi-signature-parser.h"
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
	fw = fu_firmware_get_image_default_bytes (firmware, error);
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
	gsize bufsz = 0;
	g_autofree gchar *version = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GPtrArray) dbx = NULL;

	/* use the number of checksums in the dbx as a version number, ignoring
	 * some owners that do not make sense */
	if (!fu_efivar_get_data (FU_EFIVAR_GUID_SECURITY_DATABASE, "dbx",
				 &buf, &bufsz, NULL, error))
		return FALSE;
	dbx = fu_efi_signature_parser_new (buf, bufsz,
					   FU_EFI_SIGNATURE_PARSER_FLAGS_NONE,
					   error);
	if (dbx == NULL)
		return FALSE;
	version = g_strdup_printf ("%u", fu_efi_signature_list_array_version (dbx));
	fu_device_set_version (device, version);
	fu_device_set_version_lowest (device, version);
	return TRUE;
}

static gboolean
fu_uefi_dbx_device_probe (FuDevice *device, GError **error)
{
	gsize bufsz = 0;
	g_autofree gchar *arch_up = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GPtrArray) kek = NULL;

	/* use each of the certificates in the KEK to generate the GUIDs */
	if (!fu_efivar_get_data (FU_EFIVAR_GUID_EFI_GLOBAL, "KEK",
				 &buf, &bufsz, NULL, error))
		return FALSE;
	kek = fu_efi_signature_parser_new (buf, bufsz,
					   FU_EFI_SIGNATURE_PARSER_FLAGS_NONE,
					   error);
	if (kek == NULL)
		return FALSE;
	arch_up = g_utf8_strup (EFI_MACHINE_TYPE_NAME, -1);
	for (guint i = 0; i < kek->len; i++) {
		FuEfiSignatureList *siglist = g_ptr_array_index (kek, i);
		GPtrArray *sigs = fu_efi_signature_list_get_all (siglist);
		for (guint j = 0; j < sigs->len; j++) {
			FuEfiSignature *sig = g_ptr_array_index (sigs, j);
			g_autofree gchar *checksum_up = NULL;
			g_autofree gchar *devid1 = NULL;
			g_autofree gchar *devid2 = NULL;

			checksum_up = g_utf8_strup (fu_efi_signature_get_checksum (sig), -1);
			devid1 = g_strdup_printf ("UEFI\\CRT_%s", checksum_up);
			fu_device_add_instance_id (device, devid1);
			devid2 = g_strdup_printf ("UEFI\\CRT_%s&ARCH_%s",
						  checksum_up, arch_up);
			fu_device_add_instance_id (device, devid2);
		}
	}
	return fu_uefi_dbx_device_set_version_number (device, error);
}

static void
fu_uefi_dbx_device_init (FuUefiDbxDevice *self)
{
	fu_device_set_physical_id (FU_DEVICE (self), "dbx");
	fu_device_set_name (FU_DEVICE (self), "UEFI dbx");
	fu_device_set_summary (FU_DEVICE (self), "UEFI Revocation Database");
	fu_device_set_vendor_id (FU_DEVICE (self), "UEFI:Linux Foundation");
	fu_device_set_protocol (FU_DEVICE (self), "org.uefi.dbx");
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
}

FuUefiDbxDevice *
fu_uefi_dbx_device_new (void)
{
	FuUefiDbxDevice *self;
	self = g_object_new (FU_TYPE_UEFI_DBX_DEVICE, NULL);
	return self;
}
