/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include <glib.h>
#include "fu-flashrom-opener.h"

#define FU_TYPE_FLASHROM_CONTEXT \
	(fu_flashrom_context_get_type ())
G_DECLARE_FINAL_TYPE (FuFlashromContext,
		      fu_flashrom_context, FU,
		      FLASHROM_CONTEXT, GObject)

gboolean 		 fu_flashrom_context_open (FuFlashromOpener *self,
						   FuFlashromContext **context,
						   GError **error);

gsize 			 fu_flashrom_context_get_flash_size (FuFlashromContext *self);

gboolean		 fu_flashrom_context_read_image (FuFlashromContext *self,
					   		 GBytes **data,
					   		 GError **error);
gboolean 		 fu_flashrom_context_write_image (FuFlashromContext *self,
					     		  GBytes *data,
					     		  gboolean verify,
					     		  GError **error);
gboolean 		 fu_flashrom_context_verify_image (FuFlashromContext *self,
							   GBytes *data,
							   GError **error);

gboolean 		 fu_flashrom_context_set_included_regions (FuFlashromContext *self,
								   GError **error,
								   const gchar *first_region,
								   ...) G_GNUC_NULL_TERMINATED;
