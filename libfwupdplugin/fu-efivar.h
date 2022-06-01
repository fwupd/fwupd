/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_EFIVAR_GUID_EFI_GLOBAL	  "8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define FU_EFIVAR_GUID_FWUPDATE		  "0abba7dc-e516-4167-bbf5-4d9d1c739416"
#define FU_EFIVAR_GUID_UX_CAPSULE	  "3b8c8162-188c-46a4-aec9-be43f1d65697"
#define FU_EFIVAR_GUID_SECURITY_DATABASE  "d719b2cb-3d3a-4596-a3bc-dad00e67656f"
#define FU_EFIVAR_GUID_UX_CAPSULE	  "3b8c8162-188c-46a4-aec9-be43f1d65697"
#define FU_EFIVAR_GUID_EFI_CAPSULE_REPORT "39b68c46-f7fb-441b-b6ec-16b0f69821f3"

#define FU_EFIVAR_ATTR_NON_VOLATILE			     (1 << 0)
#define FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS		     (1 << 1)
#define FU_EFIVAR_ATTR_RUNTIME_ACCESS			     (1 << 2)
#define FU_EFIVAR_ATTR_HARDWARE_ERROR_RECORD		     (1 << 3)
#define FU_EFIVAR_ATTR_AUTHENTICATED_WRITE_ACCESS	     (1 << 4)
#define FU_EFIVAR_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS (1 << 5)
#define FU_EFIVAR_ATTR_APPEND_WRITE			     (1 << 6)

gboolean
fu_efivar_supported(GError **error);
guint64
fu_efivar_space_used(GError **error);
gboolean
fu_efivar_exists(const gchar *guid, const gchar *name);
GFileMonitor *
fu_efivar_get_monitor(const gchar *guid, const gchar *name, GError **error);
gboolean
fu_efivar_get_data(const gchar *guid,
		   const gchar *name,
		   guint8 **data,
		   gsize *data_sz,
		   guint32 *attr,
		   GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_efivar_get_data_bytes(const gchar *guid, const gchar *name, guint32 *attr, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_efivar_set_data(const gchar *guid,
		   const gchar *name,
		   const guint8 *data,
		   gsize sz,
		   guint32 attr,
		   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_efivar_set_data_bytes(const gchar *guid,
			 const gchar *name,
			 GBytes *bytes,
			 guint32 attr,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_efivar_delete(const gchar *guid, const gchar *name, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_efivar_delete_with_glob(const gchar *guid,
			   const gchar *name_glob,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
GPtrArray *
fu_efivar_get_names(const gchar *guid, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_efivar_secure_boot_enabled(GError **error);
