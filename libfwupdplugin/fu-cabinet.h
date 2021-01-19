/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <xmlb.h>
#include <jcat.h>

#define FU_TYPE_CABINET (fu_cabinet_get_type ())

G_DECLARE_FINAL_TYPE (FuCabinet, fu_cabinet, FU, CABINET, GObject)

/**
 * FuCabinetParseFlags:
 * @FU_CABINET_PARSE_FLAG_NONE:		No flags set
 *
 * The flags to use when loading the cabinet.
 **/
typedef enum {
	FU_CABINET_PARSE_FLAG_NONE		= 0,
	/*< private >*/
	FU_CABINET_PARSE_FLAG_LAST
} FuCabinetParseFlags;

FuCabinet	*fu_cabinet_new			(void);
void		 fu_cabinet_set_size_max	(FuCabinet		*self,
						 guint64		 size_max);
void		 fu_cabinet_set_jcat_context	(FuCabinet		*self,
						 JcatContext		*jcat_context);
gboolean	 fu_cabinet_parse		(FuCabinet		*self,
						 GBytes			*data,
						 FuCabinetParseFlags	 flags,
						 GError			**error)
						 G_GNUC_WARN_UNUSED_RESULT;
XbSilo		*fu_cabinet_get_silo		(FuCabinet		*self);
