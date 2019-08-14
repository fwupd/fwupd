/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <fwupd.h>

#include "fu-firmware-image.h"

G_BEGIN_DECLS

#define FU_TYPE_FIRMWARE (fu_firmware_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuFirmware, fu_firmware, FU, FIRMWARE, GObject)

struct _FuFirmwareClass
{
	GObjectClass		 parent_class;
	gboolean		 (*parse)		(FuFirmware	*self,
							 GBytes		*fw,
							 guint64	 addr_start,
							 guint64	 addr_end,
							 FwupdInstallFlags flags,
							 GError		**error);
	GBytes			*(*write)		(FuFirmware	*self,
							 GError		**error);
	void			 (*to_string)		(FuFirmware	*self,
							 guint		 indent,
							 GString	*str);
	/*< private >*/
	gpointer		 padding[29];
};

FuFirmware	*fu_firmware_new			(void);
FuFirmware	*fu_firmware_new_from_bytes		(GBytes		*fw);
gchar		*fu_firmware_to_string			(FuFirmware	*self);

gboolean	 fu_firmware_parse			(FuFirmware	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_firmware_parse_full			(FuFirmware	*self,
							 GBytes		*fw,
							 guint64	 addr_start,
							 guint64	 addr_end,
							 FwupdInstallFlags flags,
							 GError		**error);
GBytes		*fu_firmware_write			(FuFirmware	*self,
							 GError		**error);

void		 fu_firmware_add_image			(FuFirmware	*self,
							 FuFirmwareImage *image);
GPtrArray	*fu_firmware_get_images			(FuFirmware	*self);
FuFirmwareImage *fu_firmware_get_image_by_id		(FuFirmware	*self,
							 const gchar	*id,
							 GError		**error);
GBytes		*fu_firmware_get_image_by_id_bytes	(FuFirmware	*self,
							 const gchar	*id,
							 GError		**error);
FuFirmwareImage *fu_firmware_get_image_by_idx		(FuFirmware	*self,
							 guint64	 idx,
							 GError		**error);
GBytes		*fu_firmware_get_image_by_idx_bytes	(FuFirmware	*self,
							 guint64	 idx,
							 GError		**error);
FuFirmwareImage	*fu_firmware_get_image_default		(FuFirmware	*self,
							 GError		**error);
GBytes		*fu_firmware_get_image_default_bytes	(FuFirmware	*self,
							 GError		**error);

G_END_DECLS
