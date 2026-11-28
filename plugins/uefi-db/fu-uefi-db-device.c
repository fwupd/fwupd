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

#define FU_UEFI_DB_DEVICE_DEFAULT_REQUIRED_FREE (16 * FU_KB) /* bytes */

/* an EFI_CERT_X509_GUID entry in the dbx revokes that exact certificate, so compare the
 * DER data rather than the key ID, which would also match a reissue using the same key */
static gboolean
fu_uefi_db_device_signature_is_revoked(GPtrArray *sigs_dbx, FuEfiSignature *sig)
{
	g_autoptr(GBytes) blob = NULL;

	if (fu_efi_signature_get_kind(sig) != FU_EFI_SIGNATURE_KIND_X509)
		return FALSE;
	blob = fu_firmware_get_bytes(FU_FIRMWARE(sig), NULL);
	if (blob == NULL)
		return FALSE;
	for (guint i = 0; i < sigs_dbx->len; i++) {
		FuEfiSignature *dbx_sig = g_ptr_array_index(sigs_dbx, i);
		g_autoptr(GBytes) dbx_blob = NULL;
		if (fu_efi_signature_get_kind(dbx_sig) != FU_EFI_SIGNATURE_KIND_X509)
			continue;
		dbx_blob = fu_firmware_get_bytes(FU_FIRMWARE(dbx_sig), NULL);
		if (dbx_blob != NULL && g_bytes_equal(blob, dbx_blob))
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_uefi_db_device_probe(FuDevice *device, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	g_autoptr(FuFirmware) siglist_db = NULL;
	g_autoptr(FuFirmware) siglist_dbx = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) sigs_dbx = NULL;
	g_autoptr(GPtrArray) sigs_dbdefault = NULL;
	g_autoptr(GPtrArray) sigs_db = NULL;

	/* FuUefiDevice->probe */
	if (!FU_DEVICE_CLASS(fu_uefi_db_device_parent_class)->probe(device, error))
		return FALSE;

	/* for specific vendors we care about the default value, but this is optional */
	if (fu_context_has_hwid_flag(ctx, "use-db-default-ids")) {
		g_autoptr(FuFirmware) siglist_dbdefault = NULL;
		g_autoptr(GError) error_local = NULL;

		siglist_dbdefault =
		    fu_efivars_get_secure_boot_siglist(fu_context_get_efivars(ctx),
						       FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB_DEFAULT,
						       &error_local);
		if (siglist_dbdefault == NULL) {
			g_debug("ignoring dbDefault: %s", error_local->message);
		} else {
			sigs_dbdefault = fu_firmware_get_images(siglist_dbdefault);
		}
	}

	/* add each subdevice */
	siglist_db = fu_efivars_get_secure_boot_siglist(fu_context_get_efivars(ctx),
							FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
							error);
	if (siglist_db == NULL)
		return FALSE;

	/* firmware will not trust a revoked certificate, so do not show it */
	siglist_dbx = fu_efivars_get_secure_boot_siglist(fu_context_get_efivars(ctx),
							 FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
							 error);
	if (siglist_dbx == NULL)
		return FALSE;
	sigs_dbx = fu_firmware_get_images(siglist_dbx);
	if (sigs_dbx != NULL) {
		g_autoptr(GPtrArray) sigs_all = fu_firmware_get_images(siglist_db);
		for (guint i = 0; i < sigs_all->len; i++) {
			FuEfiSignature *sig = g_ptr_array_index(sigs_all, i);
			if (FU_IS_EFI_X509_SIGNATURE(sig) &&
			    fu_uefi_db_device_signature_is_revoked(sigs_dbx, sig)) {
				g_debug(
				    "blocking %s [%s] as present in dbx",
				    fu_firmware_get_id(FU_FIRMWARE(sig)),
				    fu_efi_x509_signature_get_subject(FU_EFI_X509_SIGNATURE(sig)));
				fu_firmware_add_flag(FU_FIRMWARE(sig), FU_FIRMWARE_FLAG_IS_BLOCKED);
			}
		}
	}
	sigs_db = fu_efi_signature_list_get_newest(FU_EFI_SIGNATURE_LIST(siglist_db));
	for (guint i = 0; i < sigs_db->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs_db, i);
		g_autoptr(FuEfiX509Device) x509_device = NULL;
		if (fu_efi_signature_get_kind(sig) != FU_EFI_SIGNATURE_KIND_X509)
			continue;
		x509_device = fu_efi_x509_device_new(ctx, FU_EFI_X509_SIGNATURE(sig));
		fu_device_add_flag(FU_DEVICE(x509_device), FWUPD_DEVICE_FLAG_AFFECTS_FDE);
		fu_device_add_private_flag(FU_DEVICE(x509_device),
					   FU_DEVICE_PRIVATE_FLAG_REQUIRES_UNLOCK_SECUREBOOT);
		fu_device_set_physical_id(FU_DEVICE(x509_device), "db");
		fu_device_set_proxy(FU_DEVICE(x509_device), device);
		if (!fu_device_probe(FU_DEVICE(x509_device), error))
			return FALSE;
		for (guint j = 0; sigs_dbdefault != NULL && j < sigs_dbdefault->len; j++) {
			FuFirmware *default_sig = g_ptr_array_index(sigs_dbdefault, j);
			if (fu_firmware_get_id(default_sig) == NULL)
				continue;
			if (g_strcmp0(fu_firmware_get_id(default_sig),
				      fu_firmware_get_id(FU_FIRMWARE(sig))) == 0)
				continue;
			fu_device_add_instance_strup(FU_DEVICE(x509_device),
						     "CRTD",
						     fu_firmware_get_id(default_sig));
			if (!fu_device_build_instance_id(FU_DEVICE(x509_device),
							 error,
							 "UEFI",
							 "CRT",
							 "CRTD",
							 NULL))
				return FALSE;
		}
		fu_device_add_child(device, FU_DEVICE(x509_device));
	}

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
	FuContext *ctx = fu_device_get_context(device);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	return fu_efivars_set_secure_boot_siglist(fu_context_get_efivars(ctx),
						  FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
						  firmware,
						  error);
}

static void
fu_uefi_db_device_add_security_attrs_ms_uefi(FuUefiDbDevice *self, FuSecurityAttrs *attrs)
{
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));
	gboolean seen_old = FALSE;
	gboolean seen_new = FALSE;
	g_autoptr(FuSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_UEFI_DB_MS_UEFI);
	fu_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
	fu_security_attrs_append(attrs, attr);

	/* look for both versions of the Microsoft UEFI CA */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		if (fu_device_has_instance_id(child,
					      "UEFI\\CRT_7CD7437C555F89E7C2B50E21937E420C4E583E80",
					      FU_DEVICE_INSTANCE_FLAG_VISIBLE)) {
			seen_new = TRUE;
		}
		if (fu_device_has_instance_id(child,
					      "UEFI\\CRT_E30CF09DABEAB32A6E3B07A7135245DE05FFB658",
					      FU_DEVICE_INSTANCE_FLAG_VISIBLE)) {
			seen_old = TRUE;
		}
	}

	if (!seen_new && !seen_old) {
		/* user is using a custom UEFI db, just ignore this HSI attribute */
		fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
		fu_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
	} else if (seen_new) {
		fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	} else {
		fu_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
	}
}

static void
fu_uefi_db_device_add_security_attrs_production(FuUefiDbDevice *self, FuSecurityAttrs *attrs)
{
	GPtrArray *children = fu_device_get_children(FU_DEVICE(self));
	gboolean seen_insecure_platform = FALSE;
	g_autoptr(FuSecurityAttr) attr = NULL;

	/* create attr */
	attr =
	    fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_UEFI_DB_PRODUCTION);
	fu_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
	fu_security_attrs_append(attrs, attr);

	/* a test certificate was enrolled */
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		if (fu_device_has_problem(child, FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM)) {
			seen_insecure_platform = TRUE;
			break;
		}
	}
	if (seen_insecure_platform) {
		fu_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}
	fu_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_uefi_db_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuUefiDbDevice *self = FU_UEFI_DB_DEVICE(device);
	fu_uefi_db_device_add_security_attrs_ms_uefi(self, attrs);
	fu_uefi_db_device_add_security_attrs_production(self, attrs);
}

static void
fu_uefi_db_device_set_progress(FuDevice *device, FuProgress *progress)
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
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_APPLICATION_CERTIFICATE);
	fu_device_set_required_free(FU_DEVICE(self), FU_UEFI_DB_DEVICE_DEFAULT_REQUIRED_FREE);
}

static void
fu_uefi_db_device_class_init(FuUefiDbDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_uefi_db_device_probe;
	device_class->write_firmware = fu_uefi_db_device_write_firmware;
	device_class->add_security_attrs = fu_uefi_db_device_add_security_attrs;
	device_class->set_progress = fu_uefi_db_device_set_progress;
	fu_device_register_private_flag(device_class, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL);
}

FuUefiDbDevice *
fu_uefi_db_device_new(FuContext *ctx)
{
	g_autoptr(FuUefiDbDevice) self = NULL;

	g_return_val_if_fail(FU_IS_CONTEXT(ctx), NULL);

	self = g_object_new(FU_TYPE_UEFI_DB_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_kind(FU_UEFI_DEVICE(self), FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB);
	return g_steal_pointer(&self);
}
