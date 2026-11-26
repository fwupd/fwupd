/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUefiDevice"

#include "config.h"

#include "fu-bytes.h"
#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-efi-signature-list.h"
#include "fu-efivars-private.h"
#include "fu-uefi-device-private.h"

/**
 * FuUefiDevice:
 *
 * A device that represents a UEFI EFI variable.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	FuEfiSecureBootSiglistKind kind;
} FuUefiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUefiDevice, fu_uefi_device, FU_TYPE_DEVICE);

#define GET_PRIVATE(o) (fu_uefi_device_get_instance_private(o))

#define FU_UEFI_DEVICE_INHIBIT_ID_NO_EFIVARS_SPACE "no-efivars-space"

/**
 * fu_uefi_device_set_kind:
 * @self: a #FuUefiDevice
 * @kind: a #FuEfiSecureBootSiglistKind, e.g. %FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB
 *
 * Sets the well-known UEFI Secure Boot variable the device represents.
 **/
void
fu_uefi_device_set_kind(FuUefiDevice *self, FuEfiSecureBootSiglistKind kind)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *guid = fu_efivars_secure_boot_siglist_kind_to_guid(kind);
	const gchar *name = fu_efivars_secure_boot_siglist_kind_to_name(kind);

	g_return_if_fail(FU_IS_UEFI_DEVICE(self));

	/* not changed */
	if (priv->kind == kind)
		return;
	priv->kind = kind;

	/* used for quirk matching */
	if (guid != NULL)
		fu_device_add_instance_str(FU_DEVICE(self), "GUID", guid);
	if (name != NULL)
		fu_device_add_instance_str(FU_DEVICE(self), "NAME", name);
}

/**
 * fu_uefi_device_get_kind:
 * @self: a #FuUefiDevice
 *
 * Gets the well-known UEFI Secure Boot variable the device represents.
 *
 * Returns: a #FuEfiSecureBootSiglistKind, e.g. %FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB
 **/
FuEfiSecureBootSiglistKind
fu_uefi_device_get_kind(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), FU_EFI_SECURE_BOOT_SIGLIST_KIND_UNKNOWN);
	return priv->kind;
}

static void
fu_uefi_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str,
				  idt,
				  "Kind",
				  fu_efi_secure_boot_siglist_kind_to_string(priv->kind));
}

static void
fu_uefi_device_incorporate(FuDevice *device, FuDevice *donor)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevice *udonor = FU_UEFI_DEVICE(donor);
	if (fu_uefi_device_get_kind(udonor) != FU_EFI_SECURE_BOOT_SIGLIST_KIND_UNKNOWN)
		fu_uefi_device_set_kind(self, fu_uefi_device_get_kind(udonor));
}

static gboolean
fu_uefi_device_check_attrs(FuUefiDevice *self, GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	FuEfiVariableAttrs attrs = 0;
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) &&
	    !fu_device_check_fwupd_version(FU_DEVICE(self), "2.1.1"))
		return TRUE;
	if (priv->kind == FU_EFI_SECURE_BOOT_SIGLIST_KIND_UNKNOWN)
		return TRUE;

	/* if variables has been set up incorrectly do not allow update */
	if (!fu_efivars_get_attrs(fu_context_get_efivars(ctx),
				  fu_efivars_secure_boot_siglist_kind_to_guid(priv->kind),
				  fu_efivars_secure_boot_siglist_kind_to_name(priv->kind),
				  &attrs,
				  error))
		return FALSE;
	if ((attrs & FU_EFI_VARIABLE_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) == 0)
		fu_device_add_problem(FU_DEVICE(self), FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM);

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_device_probe(FuDevice *device, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);

	/* verify that the EFI variable is secure */
	if (!fu_uefi_device_check_attrs(self, error))
		return FALSE;
	return fu_device_build_instance_id_full(device,
						FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						NULL,
						"UEFI",
						"GUID",
						"NAME",
						NULL);
}
static GBytes *
fu_uefi_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (priv->kind == FU_EFI_SECURE_BOOT_SIGLIST_KIND_UNKNOWN) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no kind");
		return NULL;
	}
	return fu_efivars_get_data_bytes(fu_context_get_efivars(ctx),
					 fu_efivars_secure_boot_siglist_kind_to_guid(priv->kind),
					 fu_efivars_secure_boot_siglist_kind_to_name(priv->kind),
					 NULL,
					 error);
}

static void
fu_uefi_device_required_free_notify_cb(FuUefiDevice *self, GParamSpec *pspec, gpointer user_data)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));

	if (fu_device_get_required_free(FU_DEVICE(self)) > 0) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_context_efivars_check_free_space(
			ctx,
			fu_device_get_required_free(FU_DEVICE(self)),
			&error_local)) {
			fu_device_inhibit(FU_DEVICE(self),
					  FU_UEFI_DEVICE_INHIBIT_ID_NO_EFIVARS_SPACE,
					  error_local->message);
		} else {
			fu_device_uninhibit(FU_DEVICE(self),
					    FU_UEFI_DEVICE_INHIBIT_ID_NO_EFIVARS_SPACE);
		}
	} else {
		fu_device_uninhibit(FU_DEVICE(self), FU_UEFI_DEVICE_INHIBIT_ID_NO_EFIVARS_SPACE);
	}
}

static void
fu_uefi_device_init(FuUefiDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_REQUIRED_FREE);
	g_signal_connect(FU_DEVICE(self),
			 "notify::required-free",
			 G_CALLBACK(fu_uefi_device_required_free_notify_cb),
			 NULL);
}

static void
fu_uefi_device_class_init(FuUefiDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_uefi_device_to_string;
	device_class->probe = fu_uefi_device_probe;
	device_class->dump_firmware = fu_uefi_device_dump_firmware;
	device_class->incorporate = fu_uefi_device_incorporate;
	fu_device_register_private_flag(device_class, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL);
}

FuUefiDevice *
fu_uefi_device_new(FuContext *ctx, FuEfiSecureBootSiglistKind kind)
{
	const gchar *guid = fu_efivars_secure_boot_siglist_kind_to_guid(kind);
	const gchar *name = fu_efivars_secure_boot_siglist_kind_to_name(kind);
	g_autofree gchar *backend_id = NULL;
	g_autoptr(FuUefiDevice) self = NULL;

	g_return_val_if_fail(FU_IS_CONTEXT(ctx), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	backend_id = g_strdup_printf("%s-%s", guid, name);
	self = g_object_new(FU_TYPE_UEFI_DEVICE, "context", ctx, "backend-id", backend_id, NULL);
	fu_uefi_device_set_kind(self, kind);
	return g_steal_pointer(&self);
}
