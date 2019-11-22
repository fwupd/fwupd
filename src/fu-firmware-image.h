/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <fwupd.h>

#define FU_TYPE_FIRMWARE_IMAGE (fu_firmware_image_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuFirmwareImage, fu_firmware_image, FU, FIRMWARE_IMAGE, GObject)

struct _FuFirmwareImageClass
{
	GObjectClass		 parent_class;
	gboolean		 (*parse)	(FuFirmwareImage	*self,
						 GBytes			*fw,
						 FwupdInstallFlags	 flags,
						 GError			**error);
	void			 (*to_string)	(FuFirmwareImage	*self,
						 guint			 idt,
						 GString		*str);
	GBytes			*(*write)	(FuFirmwareImage	*self,
						 GError			**error);
	/*< private >*/
	gpointer		 padding[28];
};

#define FU_FIRMWARE_IMAGE_ID_PAYLOAD		"payload"
#define FU_FIRMWARE_IMAGE_ID_SIGNATURE		"signature"
#define FU_FIRMWARE_IMAGE_ID_HEADER		"header"

FuFirmwareImage	*fu_firmware_image_new		(GBytes			*bytes);
gchar		*fu_firmware_image_to_string	(FuFirmwareImage	*self);

const gchar	*fu_firmware_image_get_version	(FuFirmwareImage	*self);
void		 fu_firmware_image_set_version	(FuFirmwareImage	*self,
						 const gchar		*version);
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
GBytes		*fu_firmware_image_write	(FuFirmwareImage	*self,
						 GError			**error);
GBytes		*fu_firmware_image_write_chunk	(FuFirmwareImage	*self,
						 guint64		 address,
						 guint64		 chunk_sz_max,
						 GError			**error);
