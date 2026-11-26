/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-kek-device.h"

struct _FuUefiKekDevice {
	FuUefiDevice parent_instance;
};

G_DEFINE_TYPE(FuUefiKekDevice, fu_uefi_kek_device, FU_TYPE_UEFI_DEVICE)

#define FU_UEFI_KEK_DEVICE_DEFAULT_REQUIRED_FREE (4 * FU_KB) /* bytes */

static gboolean
fu_uefi_kek_device_probe(FuDevice *device, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	g_autoptr(FuFirmware) siglist = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) sigs = NULL;

	/* FuUefiDevice->probe */
	if (!FU_DEVICE_CLASS(fu_uefi_kek_device_parent_class)->probe(device, error))
		return FALSE;

	/* add each subdevice */
	siglist = fu_efivars_get_secure_boot_siglist(fu_context_get_efivars(ctx),
						     FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK,
						     error);
	if (siglist == NULL)
		return FALSE;
	if (fu_efi_signature_list_is_external(FU_EFI_SIGNATURE_LIST(siglist)))
		fu_device_add_private_flag(device, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL);
	sigs = fu_efi_signature_list_get_newest(FU_EFI_SIGNATURE_LIST(siglist));
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		g_autoptr(FuEfiX509Device) x509_device = NULL;
		if (fu_efi_signature_get_kind(sig) != FU_EFI_SIGNATURE_KIND_X509)
			continue;
		x509_device = fu_efi_x509_device_new(ctx, FU_EFI_X509_SIGNATURE(sig));
		fu_device_set_physical_id(FU_DEVICE(x509_device), "kek");
		fu_device_add_flag(FU_DEVICE(x509_device), FWUPD_DEVICE_FLAG_AFFECTS_FDE);
		fu_device_add_private_flag(FU_DEVICE(x509_device),
					   FU_DEVICE_PRIVATE_FLAG_REQUIRES_UNLOCK_SECUREBOOT);
		fu_device_add_flag(FU_DEVICE(x509_device), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
		fu_device_set_proxy(FU_DEVICE(x509_device), device);
		fu_device_add_child(device, FU_DEVICE(x509_device));
	}

	if (fu_context_has_flag(fu_device_get_context(device), FU_CONTEXT_FLAG_INSECURE_UEFI))
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM);

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_kek_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	return fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						  FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK,
						  firmware,
						  error);
}

static void
fu_uefi_kek_device_set_progress(FuDevice *device, FuProgress *progress)
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
fu_uefi_kek_device_init(FuUefiKekDevice *self)
{
	fu_device_set_physical_id(FU_DEVICE(self), "KEK");
	fu_device_set_name(FU_DEVICE(self), "UEFI Key Exchange Key");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_EFI_SIGNATURE_LIST);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_APPLICATION_CERTIFICATE);
	fu_device_set_required_free(FU_DEVICE(self), FU_UEFI_KEK_DEVICE_DEFAULT_REQUIRED_FREE);
}

static void
fu_uefi_kek_device_class_init(FuUefiKekDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_uefi_kek_device_probe;
	device_class->write_firmware = fu_uefi_kek_device_write_firmware;
	device_class->set_progress = fu_uefi_kek_device_set_progress;
}
