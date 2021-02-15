/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <fwupd.h>

#include "fu-firmware-image.h"

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
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
	GBytes			*(*write)		(FuFirmware	*self,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
	void			 (*to_string)		(FuFirmware	*self,
							 guint		 indent,
							 GString	*str);
	gboolean		 (*tokenize)		(FuFirmware	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
	gboolean		 (*build)		(FuFirmware	*self,
							 XbNode		*n,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
	/*< private >*/
	gpointer		 padding[27];
};

/**
 * FuFirmwareFlags:
 * @FU_FIRMWARE_FLAG_NONE:			No flags set
 * @FU_FIRMWARE_FLAG_DEDUPE_ID:			Dedupe imges by ID
 * @FU_FIRMWARE_FLAG_DEDUPE_IDX:		Dedupe imges by IDX
 * @FU_FIRMWARE_FLAG_HAS_CHECKSUM:		Has a CRC or checksum to test internal consistency
 * @FU_FIRMWARE_FLAG_HAS_VID_PID:		Has a vendor or product ID in the firmware
 *
 * The firmware flags.
 **/
#define FU_FIRMWARE_FLAG_NONE			(0u)		/* Since: 1.5.0 */
#define FU_FIRMWARE_FLAG_DEDUPE_ID		(1u << 0)	/* Since: 1.5.0 */
#define FU_FIRMWARE_FLAG_DEDUPE_IDX		(1u << 1)	/* Since: 1.5.0 */
#define FU_FIRMWARE_FLAG_HAS_CHECKSUM		(1u << 2)	/* Since: 1.5.6 */
#define FU_FIRMWARE_FLAG_HAS_VID_PID		(1u << 3)	/* Since: 1.5.6 */
typedef guint64 FuFirmwareFlags;

const gchar	*fu_firmware_flag_to_string		(FuFirmwareFlags flag);
FuFirmwareFlags	 fu_firmware_flag_from_string		(const gchar	*flag);

FuFirmware	*fu_firmware_new			(void);
FuFirmware	*fu_firmware_new_from_bytes		(GBytes		*fw);
FuFirmware	*fu_firmware_new_from_gtypes		(GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error,
							 ...);
gchar		*fu_firmware_to_string			(FuFirmware	*self);
const gchar	*fu_firmware_get_version		(FuFirmware	*self);
void		 fu_firmware_set_version		(FuFirmware	*self,
							 const gchar	*version);
guint64		 fu_firmware_get_version_raw		(FuFirmware	*self);
void		 fu_firmware_set_version_raw		(FuFirmware	*self,
							 guint64	 version_raw);
void		 fu_firmware_add_flag			(FuFirmware	*firmware,
							 FuFirmwareFlags flag);
gboolean	 fu_firmware_has_flag			(FuFirmware	*firmware,
							 FuFirmwareFlags flag);

gboolean	 fu_firmware_tokenize			(FuFirmware	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_firmware_build			(FuFirmware	*self,
							 XbNode		*n,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_firmware_parse			(FuFirmware	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_firmware_parse_file			(FuFirmware	*self,
							 GFile		*file,
							 FwupdInstallFlags flags,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_firmware_parse_full			(FuFirmware	*self,
							 GBytes		*fw,
							 guint64	 addr_start,
							 guint64	 addr_end,
							 FwupdInstallFlags flags,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
GBytes		*fu_firmware_write			(FuFirmware	*self,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 fu_firmware_write_file			(FuFirmware	*self,
							 GFile		*file,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;

void		 fu_firmware_add_image			(FuFirmware	*self,
							 FuFirmwareImage *img);
gboolean	 fu_firmware_remove_image		(FuFirmware	*self,
							 FuFirmwareImage *img,
							 GError		**error);
gboolean	 fu_firmware_remove_image_by_idx	(FuFirmware	*self,
							 guint64	 idx,
							 GError		**error);
gboolean	 fu_firmware_remove_image_by_id		(FuFirmware	*self,
							 const gchar	*id,
							 GError		**error);
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
FuFirmwareImage	*fu_firmware_get_image_by_checksum	(FuFirmware	*self,
							 const gchar	*checksum,
							 GError		**error);
FuFirmwareImage	*fu_firmware_get_image_default		(FuFirmware	*self,
							 GError		**error);
GBytes		*fu_firmware_get_image_default_bytes	(FuFirmware	*self,
							 GError		**error);
