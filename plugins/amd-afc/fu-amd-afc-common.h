/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

/* internal AFC model shared by the container, HII, and string parsers */
#include "fu-amd-afc.h"

G_BEGIN_DECLS

#define AFC_IFR_FLAG_READ_ONLY 0x01U

typedef enum {
	AFC_SETTING_ENUMERATION,
	AFC_SETTING_INTEGER,
} FuAmdAfcSettingKind;

typedef struct {
	gchar *name;
	guint64 value;
} FuAmdAfcOption;

typedef struct {
	FuAmdAfcSettingKind kind;
	gchar *display_name;
	gchar *language;
	GPtrArray *path;    /* gchar * */
	GPtrArray *options; /* FuAmdAfcOption * */
	guint16 question_id;
	guint8 question_flags;
	gboolean has_current;
	guint64 current;
	guint64 minimum;
	guint64 maximum;
	guint64 step;
	gboolean has_default;
	guint64 default_value;
} FuAmdAfcSetting;

typedef struct {
	gchar *name;
	guint16 id;
	GArray *settings; /* guint */
	GArray *refs;	  /* guint16 */
	gboolean visited;
} FuAmdAfcForm;

typedef struct {
	guint16 id;
	GBytes *data;
} FuAmdAfcVarstore;

typedef struct {
	guint setting;
	gchar *value;
} FuAmdAfcPending;

typedef struct {
	GPtrArray *path; /* gchar * */
	gchar *value;
} FuAmdAfcConfigEntry;

struct _FuAmdAfcState {
	grefcount refcount;
	FuEfivars *efivars;
	FuBiosSettings *bios_settings; /* weak */
	GPtrArray *strings;   /* gchar *, index is HII string ID */
	GPtrArray *forms;     /* FuAmdAfcForm * */
	GPtrArray *varstores; /* FuAmdAfcVarstore * */
	GPtrArray *settings;  /* FuAmdAfcSetting * */
	GPtrArray *pending;   /* FuAmdAfcPending * */
	gchar *formset_name;
	gchar *language;
	guint16 revision;
};

void
fu_amd_afc_hii_setting_free(FuAmdAfcSetting *setting);
void
fu_amd_afc_hii_form_free(FuAmdAfcForm *form);
void
fu_amd_afc_hii_varstore_free(FuAmdAfcVarstore *varstore);

gboolean
fu_amd_afc_hii_bounds(gsize bufsz, gsize offset, gsize length, GError **error);
const gchar *
fu_amd_afc_hii_strings_get_string(FuAmdAfcState *self, guint16 id, GError **error);

gboolean
fu_amd_afc_hii_strings_parse_package(FuAmdAfcState *self,
				     const guint8 *buf,
				     gsize bufsz,
				     GError **error);
gboolean
fu_amd_afc_hii_parse_varstores(FuAmdAfcState *self, const guint8 *buf, gsize bufsz, GError **error);
gboolean
fu_amd_afc_hii_parse_forms(FuAmdAfcState *self, const guint8 *buf, gsize bufsz, GError **error);
gboolean
fu_amd_afc_hii_assign_form_paths(FuAmdAfcState *self,
				 guint form_idx,
				 GPtrArray *path,
				 GError **error);
gboolean
fu_amd_afc_config_id(GByteArray *strings,
		     guint16 *count,
		     const gchar *value,
		     guint16 *id,
		     GError **error);
GPtrArray *
fu_amd_afc_config_parse(GBytes *bytes, GError **error);
gboolean
fu_amd_afc_config_validate(GBytes *bytes, GError **error);

G_END_DECLS
