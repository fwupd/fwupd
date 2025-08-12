/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-dbx-common.h"
#include "fu-uefi-dbx-device.h"
#include "fu-uefi-dbx-snapd-notifier.h"

struct _FuUefiDbxDevice {
	FuUefiDevice parent_instance;
	FuUefiDbxSnapdNotifier *snapd_notifier;
};

G_DEFINE_TYPE(FuUefiDbxDevice, fu_uefi_dbx_device, FU_TYPE_UEFI_DEVICE)

#define FU_UEFI_DBX_DEVICE_DEFAULT_REQUIRED_FREE (30 * 1024) /* bytes */

void
fu_uefi_dbx_device_set_snapd_notifier(FuUefiDbxDevice *self, FuUefiDbxSnapdNotifier *obs)
{
	g_set_object(&self->snapd_notifier, obs);
}

static gboolean
fu_uefi_dbx_device_maybe_notify_snapd_prepare(FuUefiDbxDevice *self, GBytes *data, GError **error)
{
	if (self->snapd_notifier == NULL)
		return TRUE;

	return fu_uefi_dbx_snapd_notifier_dbx_update_prepare(self->snapd_notifier, data, error);
}

static gboolean
fu_uefi_dbx_device_maybe_notify_snapd_cleanup(FuUefiDbxDevice *self, GError **error)
{
	if (self->snapd_notifier == NULL)
		return TRUE;

	return fu_uefi_dbx_snapd_notifier_dbx_update_cleanup(self->snapd_notifier, error);
}

static gboolean
fu_uefi_dbx_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags install_flags,
				  GError **error)
{
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	if (!fu_uefi_dbx_device_maybe_notify_snapd_prepare(FU_UEFI_DBX_DEVICE(device), fw, error))
		return FALSE;

	/* write entire chunk to efivarsfs */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_uefi_device_set_efivar_bytes(
		FU_UEFI_DEVICE(device),
		FU_EFIVARS_GUID_SECURITY_DATABASE,
		"dbx",
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

static gboolean
fu_uefi_dbx_device_set_checksum(FuUefiDbxDevice *self, const gchar *csum, GError **error)
{
	/* used for md-set-version, but only when the device is added to the daemon */
	fu_device_add_checksum(FU_DEVICE(self), csum);

	/* for operating offline, with no /usr/share/fwupd/remotes.d archives */
	fu_device_add_instance_strup(FU_DEVICE(self), "CSUM", csum);
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "UEFI",
					      "CSUM",
					      NULL))
		return FALSE;

	/* this makes debugging easier */
	if (fu_device_get_version(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *csum_trunc = g_strndup(csum, 8);
		g_autofree gchar *summary =
		    g_strdup_printf("UEFI revocation database %s", csum_trunc);
		fu_device_set_summary(FU_DEVICE(self), summary);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_dbx_device_ensure_checksum(FuUefiDbxDevice *self, GError **error)
{
	g_autoptr(GBytes) dbx_blob = NULL;
	g_autoptr(FuFirmware) dbx = fu_efi_signature_list_new();
	g_autoptr(GPtrArray) sigs = NULL;

	/* use the number of checksums in the dbx as a version number, ignoring
	 * some owners that do not make sense */
	dbx_blob = fu_uefi_device_get_efivar_bytes(FU_UEFI_DEVICE(self),
						   FU_EFIVARS_GUID_SECURITY_DATABASE,
						   "dbx",
						   NULL,
						   error);
	if (dbx_blob == NULL)
		return FALSE;
	if (!fu_firmware_parse_bytes(dbx, dbx_blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, error))
		return FALSE;

	/* add the last checksum to the device */
	sigs = fu_firmware_get_images(dbx);

	for (guint i = sigs->len; i > 0; i--) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i - 1);
		const gchar *owner = fu_efi_signature_get_owner(sig);
		g_autofree gchar *csum =
		    fu_firmware_get_checksum(FU_FIRMWARE(sig), G_CHECKSUM_SHA256, NULL);

		if (g_strcmp0(owner, FU_EFI_SIGNATURE_GUID_MICROSOFT) != 0) {
			g_debug("skipping dbx entry %s as non-microsoft (%s)", csum, owner);
			continue;
		}

		if (csum != NULL) {
			if (!fu_uefi_dbx_device_set_checksum(self, csum, error))
				return FALSE;
			break;
		}
	}

	/* special entry for "empty" */
	if (sigs->len == 1) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, 0);
		const gchar *owner = fu_efi_signature_get_owner(sig);
		if (g_strcmp0(owner, FU_EFI_SIGNATURE_GUID_ZERO) == 0)
			fu_device_set_version_raw(FU_DEVICE(self), 0);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_dbx_device_reload(FuDevice *device, GError **error)
{
	FuUefiDbxDevice *self = FU_UEFI_DBX_DEVICE(device);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_VERSION);
	return fu_uefi_dbx_device_ensure_checksum(self, error);
}

static void
fu_uefi_dbx_device_version_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	fu_device_set_version_lowest(device, fu_device_get_version(device));
}

static FuFirmware *
fu_uefi_dbx_device_prepare_firmware(FuDevice *device,
				    GInputStream *stream,
				    FuProgress *progress,
				    FuFirmwareParseFlags flags,
				    GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();

	/* parse dbx */
	if (!fu_firmware_parse_stream(siglist, stream, 0x0, flags, error)) {
		g_prefix_error_literal(error, "cannot parse DBX update: ");
		return NULL;
	}

	/* validate this is safe to apply */
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_VERIFY);
		if (!fu_uefi_dbx_signature_list_validate(ctx,
							 FU_EFI_SIGNATURE_LIST(siglist),
							 flags,
							 error)) {
			g_prefix_error_literal(error,
					       "Blocked executable in the ESP, "
					       "ensure grub and shim are up to date: ");
			return NULL;
		}
	}

	/* default blob */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_uefi_dbx_device_probe(FuDevice *device, GError **error)
{
	FuUefiDbxDevice *self = FU_UEFI_DBX_DEVICE(device);
	g_autoptr(FuFirmware) kek = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) sigs = NULL;

	/* use each of the certificates in the KEK to generate the GUIDs */
	kek = fu_device_read_firmware(device,
				      progress,
				      FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM,
				      error);
	if (kek == NULL) {
		g_prefix_error_literal(error, "failed to parse KEK: ");
		return FALSE;
	}
	fu_device_add_instance_strup(device, "ARCH", fu_uefi_dbx_get_efi_arch());

	sigs = fu_firmware_get_images(kek);
	for (guint j = 0; j < sigs->len; j++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, j);
		g_autofree gchar *checksum = NULL;

		checksum = fu_firmware_get_checksum(FU_FIRMWARE(sig), G_CHECKSUM_SHA256, error);
		if (checksum == NULL)
			return FALSE;
		fu_device_add_instance_strup(device, "CRT", checksum);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 "UEFI",
						 "CRT",
						 NULL);
		fu_device_build_instance_id(device, NULL, "UEFI", "CRT", "ARCH", NULL);
	}
	return fu_uefi_dbx_device_ensure_checksum(self, error);
}

static void
fu_uefi_dbx_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_uefi_dbx_device_cleanup(FuDevice *self,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	if (!fu_uefi_dbx_device_maybe_notify_snapd_cleanup(FU_UEFI_DBX_DEVICE(self), error))
		return FALSE;

	return TRUE;
}

static gchar *
fu_uefi_dbx_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint64(version_raw, fu_device_get_version_format(device));
}

static void
fu_uefi_dbx_device_init(FuUefiDbxDevice *self)
{
	fu_device_set_physical_id(FU_DEVICE(self), "dbx");
	fu_device_set_name(FU_DEVICE(self), "UEFI dbx");
	fu_device_set_summary(FU_DEVICE(self), "UEFI revocation database");
	fu_device_add_protocol(FU_DEVICE(self), "org.uefi.dbx2");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_install_duration(FU_DEVICE(self), 1);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_EFI_SIGNATURE_LIST);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_APPLICATION_CERTIFICATE);
	fu_device_set_required_free(FU_DEVICE(self), FU_UEFI_DBX_DEVICE_DEFAULT_REQUIRED_FREE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_AFFECTS_FDE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_ONLY_CHECKSUM);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_VERSION);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN);
	g_signal_connect(FWUPD_DEVICE(self),
			 "notify::version",
			 G_CALLBACK(fu_uefi_dbx_device_version_notify_cb),
			 NULL);
}

static void
fu_uefi_dbx_device_finalize(GObject *object)
{
	FuUefiDbxDevice *self = FU_UEFI_DBX_DEVICE(object);

	if (self->snapd_notifier != NULL)
		g_object_unref(self->snapd_notifier);

	G_OBJECT_CLASS(fu_uefi_dbx_device_parent_class)->finalize(object);
}

static void
fu_uefi_dbx_device_class_init(FuUefiDbxDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_uefi_dbx_device_probe;
	device_class->reload = fu_uefi_dbx_device_reload;
	device_class->write_firmware = fu_uefi_dbx_device_write_firmware;
	device_class->prepare_firmware = fu_uefi_dbx_device_prepare_firmware;
	device_class->set_progress = fu_uefi_dbx_device_set_progress;
	device_class->cleanup = fu_uefi_dbx_device_cleanup;
	device_class->convert_version = fu_uefi_dbx_device_convert_version;

	object_class->finalize = fu_uefi_dbx_device_finalize;
}
