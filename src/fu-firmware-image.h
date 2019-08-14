/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <fwupd.h>

G_BEGIN_DECLS

#define FU_TYPE_FIRMWARE_IMAGE (fu_firmware_image_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuFirmwareImage, fu_firmware_image, FU, FIRMWARE_IMAGE, GObject)

struct _FuFirmwareImageClass
{
	GObjectClass		 parent_class;
	gboolean		 (*parse)	(FuFirmwareImage	*self,
						 GBytes			*fw,
						 FwupdInstallFlags	 flags,
						 GError			**error);
	/*< private >*/
	gpointer		 padding[30];
};

FuFirmwareImage	*fu_firmware_image_new		(GBytes			*bytes);
gchar		*fu_firmware_image_to_string	(FuFirmwareImage	*selfz);

const gchar	*fu_firmware_image_get_id	(FuFirmwareImage	*self);
void		 fu_firmware_image_set_id	(FuFirmwareImage	*self,
						 const gchar		*id);
guint64		 fu_firmware_image_get_addr	(FuFirmwareImage	*self);
void		 fu_firmware_image_set_addr	(FuFirmwareImage	*self,
						 guint64		 addr);
guint64		 fu_firmware_image_get_idx	(FuFirmwareImage	*self);
void		 fu_firmware_image_set_idx	(FuFirmwareImage	*self,
						 guint64		 idx);
void		 fu_firmware_image_set_bytes	(FuFirmwareImage	*self,
						 GBytes			*bytes);
GBytes		*fu_firmware_image_get_bytes	(FuFirmwareImage	*self,
						 GError			**error);
GBytes		*fu_firmware_image_get_bytes_chunk(FuFirmwareImage	*self,
						 guint64		 address,
						 guint64		 chunk_sz_max,
						 GError			**error);

G_END_DECLS
