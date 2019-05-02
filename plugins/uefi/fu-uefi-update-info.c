/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-uefi-devpath.h"
#include "fu-uefi-update-info.h"
#include "fu-uefi-common.h"
#include "fu-ucs2.h"

#include "fwupd-error.h"

struct _FuUefiUpdateInfo {
	GObject			 parent_instance;
	guint32			 version;
	gchar			*guid;
	gchar			*capsule_fn;
	guint32			 capsule_flags;
	guint64			 hw_inst;
	FuUefiUpdateInfoStatus	 status;
};

G_DEFINE_TYPE (FuUefiUpdateInfo, fu_uefi_update_info, G_TYPE_OBJECT)

const gchar *
fu_uefi_update_info_status_to_string (FuUefiUpdateInfoStatus status)
{
	if (status == FU_UEFI_UPDATE_INFO_STATUS_ATTEMPT_UPDATE)
		return "attempt-update";
	if (status == FU_UEFI_UPDATE_INFO_STATUS_ATTEMPTED)
		return "attempted";
	return "unknown";
}

static gchar *
fu_uefi_update_info_parse_dp (const guint8 *buf, gsize sz, GError **error)
{
	GBytes *dp_data;
	const gchar *data;
	gsize ucs2sz = 0;
	g_autofree gchar *relpath = NULL;
	g_autofree guint16 *ucs2file = NULL;
	g_autoptr(GPtrArray) dps = NULL;

	g_return_val_if_fail (buf != NULL, NULL);
	g_return_val_if_fail (sz != 0, NULL);

	/* get all headers */
	dps = fu_uefi_devpath_parse (buf, sz, FU_UEFI_DEVPATH_PARSE_FLAG_REPAIR, error);
	if (dps == NULL)
		return NULL;
	dp_data = fu_uefi_devpath_find_data (dps,
					     EFIDP_MEDIA_TYPE,
					     EFIDP_MEDIA_FILE,
					     error);
	if (dp_data == NULL)
		return NULL;

	/* convert to UTF-8 */
	data = g_bytes_get_data (dp_data, &ucs2sz);
	ucs2file = g_new0 (guint16, (ucs2sz / 2) + 1);
	memcpy (ucs2file, data, ucs2sz);
	relpath = fu_ucs2_to_uft8 (ucs2file, ucs2sz / sizeof (guint16));
	if (relpath == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot convert to UTF-8");
		return NULL;
	}
	g_strdelimit (relpath, "\\", '/');
	return g_steal_pointer (&relpath);
}

gboolean
fu_uefi_update_info_parse (FuUefiUpdateInfo *self, const guint8 *buf, gsize sz, GError **error)
{
	efi_update_info_t info;
	efi_guid_t guid_tmp;

	g_return_val_if_fail (FU_IS_UEFI_UPDATE_INFO (self), FALSE);

	if (sz < sizeof(efi_update_info_t)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "EFI variable is corrupt");
		return FALSE;
	}
	memcpy (&info, buf, sizeof(info));
	self->version = info.update_info_version;
	self->capsule_flags = info.capsule_flags;
	self->hw_inst = info.hw_inst;
	self->status = info.status;
	memcpy (&guid_tmp, &info.guid, sizeof(efi_guid_t));
	if (efi_guid_to_str (&guid_tmp, &self->guid) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to convert GUID");
		return FALSE;
	}
	if (sz > sizeof(efi_update_info_t)) {
		self->capsule_fn = fu_uefi_update_info_parse_dp (buf + sizeof(efi_update_info_t),
								 sz - sizeof(efi_update_info_t),
								 error);
		if (self->capsule_fn == NULL)
			return FALSE;
	}
	return TRUE;
}

const gchar *
fu_uefi_update_info_get_guid (FuUefiUpdateInfo *self)
{
	g_return_val_if_fail (FU_IS_UEFI_UPDATE_INFO (self), NULL);
	return self->guid;
}

const gchar *
fu_uefi_update_info_get_capsule_fn (FuUefiUpdateInfo *self)
{
	g_return_val_if_fail (FU_IS_UEFI_UPDATE_INFO (self), NULL);
	return self->capsule_fn;
}

guint32
fu_uefi_update_info_get_version (FuUefiUpdateInfo *self)
{
	g_return_val_if_fail (FU_IS_UEFI_UPDATE_INFO (self), 0);
	return self->version;
}

guint32
fu_uefi_update_info_get_capsule_flags (FuUefiUpdateInfo *self)
{
	g_return_val_if_fail (FU_IS_UEFI_UPDATE_INFO (self), 0);
	return self->capsule_flags;
}

guint64
fu_uefi_update_info_get_hw_inst (FuUefiUpdateInfo *self)
{
	g_return_val_if_fail (FU_IS_UEFI_UPDATE_INFO (self), 0);
	return self->hw_inst;
}

FuUefiUpdateInfoStatus
fu_uefi_update_info_get_status (FuUefiUpdateInfo *self)
{
	g_return_val_if_fail (FU_IS_UEFI_UPDATE_INFO (self), 0);
	return self->status;
}

static void
fu_uefi_update_info_finalize (GObject *object)
{
	FuUefiUpdateInfo *self = FU_UEFI_UPDATE_INFO (object);
	g_free (self->guid);
	g_free (self->capsule_fn);
	G_OBJECT_CLASS (fu_uefi_update_info_parent_class)->finalize (object);
}

static void
fu_uefi_update_info_class_init (FuUefiUpdateInfoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_uefi_update_info_finalize;
}

static void
fu_uefi_update_info_init (FuUefiUpdateInfo *self)
{
}

FuUefiUpdateInfo *
fu_uefi_update_info_new (void)
{
	FuUefiUpdateInfo *self;
	self = g_object_new (FU_TYPE_UEFI_UPDATE_INFO, NULL);
	return FU_UEFI_UPDATE_INFO (self);
}
