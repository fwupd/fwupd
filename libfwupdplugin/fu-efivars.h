/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efi-load-option.h"
#include "fu-volume.h"

#define FU_TYPE_EFIVARS (fu_efivars_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfivars, fu_efivars, FU, EFIVARS, GObject)

struct _FuEfivarsClass {
	GObjectClass parent_class;
	gboolean (*supported)(FuEfivars *self, GError **error) G_GNUC_NON_NULL(1);
	guint64 (*space_used)(FuEfivars *self, GError **error) G_GNUC_NON_NULL(1);
	gboolean (*exists)(FuEfivars *self, const gchar *guid, const gchar *name)
	    G_GNUC_NON_NULL(1, 2);
	GFileMonitor *(*get_monitor)(FuEfivars *self,
				     const gchar *guid,
				     const gchar *name,
				     GError **error)G_GNUC_NON_NULL(1, 2, 3);
	gboolean (*get_data)(FuEfivars *self,
			     const gchar *guid,
			     const gchar *name,
			     guint8 **data,
			     gsize *data_sz,
			     guint32 *attr,
			     GError **error) G_GNUC_NON_NULL(1, 2, 3);
	gboolean (*set_data)(FuEfivars *self,
			     const gchar *guid,
			     const gchar *name,
			     const guint8 *data,
			     gsize sz,
			     guint32 attr,
			     GError **error) G_GNUC_NON_NULL(1, 2, 3);
	gboolean (*delete)(FuEfivars *self, const gchar *guid, const gchar *name, GError **error)
	    G_GNUC_NON_NULL(1, 2, 3);
	gboolean (*delete_with_glob)(FuEfivars *self,
				     const gchar *guid,
				     const gchar *name_glob,
				     GError **error) G_GNUC_NON_NULL(1, 2, 3);
	GPtrArray *(*get_names)(FuEfivars *self,
				const gchar *guid,
				GError **error)G_GNUC_NON_NULL(1, 2);
};

#define FU_EFIVARS_GUID_EFI_GLOBAL	   "8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define FU_EFIVARS_GUID_FWUPDATE	   "0abba7dc-e516-4167-bbf5-4d9d1c739416"
#define FU_EFIVARS_GUID_UX_CAPSULE	   "3b8c8162-188c-46a4-aec9-be43f1d65697"
#define FU_EFIVARS_GUID_SECURITY_DATABASE  "d719b2cb-3d3a-4596-a3bc-dad00e67656f"
#define FU_EFIVARS_GUID_EFI_CAPSULE_REPORT "39b68c46-f7fb-441b-b6ec-16b0f69821f3"
#define FU_EFIVARS_GUID_SHIM		   "605dab50-e046-4300-abb6-3dd810dd8b23"

#define FU_EFIVARS_ATTR_NON_VOLATILE			      (1 << 0)
#define FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS		      (1 << 1)
#define FU_EFIVARS_ATTR_RUNTIME_ACCESS			      (1 << 2)
#define FU_EFIVARS_ATTR_HARDWARE_ERROR_RECORD		      (1 << 3)
#define FU_EFIVARS_ATTR_AUTHENTICATED_WRITE_ACCESS	      (1 << 4)
#define FU_EFIVARS_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS (1 << 5)
#define FU_EFIVARS_ATTR_APPEND_WRITE			      (1 << 6)

FuEfivars *
fu_efivars_new(void);
gboolean
fu_efivars_supported(FuEfivars *self, GError **error) G_GNUC_NON_NULL(1);
guint64
fu_efivars_space_used(FuEfivars *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_exists(FuEfivars *self, const gchar *guid, const gchar *name) G_GNUC_NON_NULL(1, 2);
GFileMonitor *
fu_efivars_get_monitor(FuEfivars *self, const gchar *guid, const gchar *name, GError **error)
    G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_efivars_get_data(FuEfivars *self,
		    const gchar *guid,
		    const gchar *name,
		    guint8 **data,
		    gsize *data_sz,
		    guint32 *attr,
		    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
GBytes *
fu_efivars_get_data_bytes(FuEfivars *self,
			  const gchar *guid,
			  const gchar *name,
			  guint32 *attr,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_efivars_set_data(FuEfivars *self,
		    const gchar *guid,
		    const gchar *name,
		    const guint8 *data,
		    gsize sz,
		    guint32 attr,
		    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_efivars_set_data_bytes(FuEfivars *self,
			  const gchar *guid,
			  const gchar *name,
			  GBytes *bytes,
			  guint32 attr,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_efivars_delete(FuEfivars *self, const gchar *guid, const gchar *name, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_efivars_delete_with_glob(FuEfivars *self,
			    const gchar *guid,
			    const gchar *name_glob,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
GPtrArray *
fu_efivars_get_names(FuEfivars *self, const gchar *guid, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_efivars_get_secure_boot(FuEfivars *self, gboolean *enabled, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_efivars_get_boot_next(FuEfivars *self, guint16 *idx, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_set_boot_next(FuEfivars *self, guint16 idx, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_get_boot_current(FuEfivars *self, guint16 *idx, GError **error) G_GNUC_NON_NULL(1);
GArray *
fu_efivars_get_boot_order(FuEfivars *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_set_boot_order(FuEfivars *self, GArray *order, GError **error) G_GNUC_NON_NULL(1, 2);
GBytes *
fu_efivars_get_boot_data(FuEfivars *self, guint16 idx, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_set_boot_data(FuEfivars *self, guint16 idx, GBytes *blob, GError **error)
    G_GNUC_NON_NULL(1);
FuEfiLoadOption *
fu_efivars_get_boot_entry(FuEfivars *self, guint16 idx, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_set_boot_entry(FuEfivars *self, guint16 idx, FuEfiLoadOption *entry, GError **error)
    G_GNUC_NON_NULL(1, 3);
GPtrArray *
fu_efivars_get_boot_entries(FuEfivars *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_create_boot_entry_for_volume(FuEfivars *self,
					guint16 idx,
					FuVolume *volume,
					const gchar *name,
					const gchar *target,
					GError **error) G_GNUC_NON_NULL(1, 3, 4, 5);
