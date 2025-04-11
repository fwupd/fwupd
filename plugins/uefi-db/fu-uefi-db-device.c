/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-db-device.h"

struct _FuUefiDbDevice {
	FuUefiDevice parent_instance;
};

G_DEFINE_TYPE(FuUefiDbDevice, fu_uefi_db_device, FU_TYPE_UEFI_DEVICE)

static gboolean
fu_uefi_db_device_probe(FuDevice *device, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	g_autoptr(FuFirmware) siglist = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) sigs = NULL;

	/* FuUefiDevice->probe */
	if (!FU_DEVICE_CLASS(fu_uefi_db_device_parent_class)->probe(device, error))
		return FALSE;

	/* add each subdevice */
	siglist = fu_device_read_firmware(device, progress, error);
	if (siglist == NULL) {
		g_prefix_error(error, "failed to parse db: ");
		return FALSE;
	}
	sigs = fu_efi_signature_list_get_newest(FU_EFI_SIGNATURE_LIST(siglist));
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		g_autoptr(FuEfiX509Device) x509_device = NULL;
		if (fu_efi_signature_get_kind(sig) != FU_EFI_SIGNATURE_KIND_X509)
			continue;
		x509_device = fu_efi_x509_device_new(ctx, FU_EFI_X509_SIGNATURE(sig));
		fu_device_set_physical_id(FU_DEVICE(x509_device), "db");
		fu_device_set_proxy(FU_DEVICE(x509_device), device);
		fu_device_add_child(device, FU_DEVICE(x509_device));
	}

	/* set in the subdevice */
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY);

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_db_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* write entire chunk to efivarsfs */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_uefi_device_set_efivar_bytes(
		FU_UEFI_DEVICE(device),
		FU_EFIVARS_GUID_SECURITY_DATABASE,
		fu_device_get_physical_id(device),
		fw,
		FU_EFIVARS_ATTR_APPEND_WRITE |
		    FU_EFIVARS_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS |
		    FU_EFIVARS_ATTR_RUNTIME_ACCESS | FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS |
		    FU_EFIVARS_ATTR_NON_VOLATILE,
		error)) {
		return FALSE;
	}

	/* success! */
	return TRUE;
}

static void
fu_uefi_db_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	GPtrArray *children = fu_device_get_children(device);
	gboolean seen_old = FALSE;
	gboolean seen_new = FALSE;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(device, FWUPD_SECURITY_ATTR_ID_UEFI_DB);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
	fu_security_attrs_append(attrs, attr);

	/* look for both versions of the Microsoft UEFI CA */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		if (fu_device_has_instance_id(child,
					      "UEFI\\CRT_A5B7C551CEDC06B94D0C5B920F473E03C2F142F2",
					      FU_DEVICE_INSTANCE_FLAG_VISIBLE)) {
			seen_new = TRUE;
			break;
		}
		if (fu_device_has_instance_id(child,
					      "UEFI\\CRT_03DE12BE14CA397DF20CEE646C7D9B727FCCE5F8",
					      FU_DEVICE_INSTANCE_FLAG_VISIBLE)) {
			seen_old = TRUE;
			break;
		}
	}

	if (!seen_new && !seen_old) {
		/* user is using a custom UEFI db, just ignore this HSI attribute */
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
	} else if (seen_new) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	} else {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	}
}

static void
fu_uefi_db_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_uefi_db_device_init(FuUefiDbDevice *self)
{
	fu_device_set_physical_id(FU_DEVICE(self), "db");
	fu_device_set_name(FU_DEVICE(self), "UEFI Signature Database");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_EFI_SIGNATURE_LIST);
	fu_device_add_icon(FU_DEVICE(self), "application-certificate");
}

static void
fu_uefi_db_device_class_init(FuUefiDbDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_uefi_db_device_probe;
	device_class->write_firmware = fu_uefi_db_device_write_firmware;
	device_class->add_security_attrs = fu_uefi_db_device_add_security_attrs;
	device_class->set_progress = fu_uefi_db_device_set_progress;
}
