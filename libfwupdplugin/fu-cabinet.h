/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <jcat.h>
#include <xmlb.h>

#define FU_TYPE_CABINET (fu_cabinet_get_type())

G_DECLARE_FINAL_TYPE(FuCabinet, fu_cabinet, FU, CABINET, GObject)

/**
 * FuCabinetParseFlags:
 * @FU_CABINET_PARSE_FLAG_NONE:		No flags set
 *
 * The flags to use when loading the cabinet.
 **/
typedef enum {
	FU_CABINET_PARSE_FLAG_NONE = 0,
	/*< private >*/
	FU_CABINET_PARSE_FLAG_LAST
} FuCabinetParseFlags;

/**
 * FuCabinetExportFlags:
 * @FU_CABINET_EXPORT_FLAG_NONE:		No flags set
 *
 * The flags to use when exporting the archive.
 **/
typedef enum {
	FU_CABINET_EXPORT_FLAG_NONE = 0,
	/*< private >*/
	FU_CABINET_EXPORT_FLAG_LAST
} FuCabinetExportFlags;

/**
 * FuCabinetSignFlags:
 * @FU_CABINET_SIGN_FLAG_NONE:		No flags set
 *
 * The flags to use when signing the archive.
 **/
typedef enum {
	FU_CABINET_SIGN_FLAG_NONE = 0,
	/*< private >*/
	FU_CABINET_SIGN_FLAG_LAST
} FuCabinetSignFlags;

FuCabinet *
fu_cabinet_new(void);
void
fu_cabinet_set_size_max(FuCabinet *self, guint64 size_max);
void
fu_cabinet_set_jcat_context(FuCabinet *self, JcatContext *jcat_context);
gboolean
fu_cabinet_parse(FuCabinet *self, GBytes *data, FuCabinetParseFlags flags, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_cabinet_sign(FuCabinet *self,
		GBytes *cert,
		GBytes *privkey,
		FuCabinetSignFlags flags,
		GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_cabinet_add_file(FuCabinet *self, const gchar *basename, GBytes *data);
GBytes *
fu_cabinet_get_file(FuCabinet *self, const gchar *basename, GError **error);
GBytes *
fu_cabinet_export(FuCabinet *self,
		  FuCabinetExportFlags flags,
		  GError **error) G_GNUC_WARN_UNUSED_RESULT;
XbSilo *
fu_cabinet_get_silo(FuCabinet *self);
