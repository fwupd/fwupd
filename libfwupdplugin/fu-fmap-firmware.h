/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_FMAP_FIRMWARE_STRLEN 32 /* maximum length for strings, */
				   /* including null-terminator */

#define FU_TYPE_FMAP_FIRMWARE (fu_fmap_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuFmapFirmware, fu_fmap_firmware, FU, FMAP_FIRMWARE, FuFirmware)

struct _FuFmapFirmwareClass {
	FuFirmwareClass parent_class;
	gboolean (*parse)(FuFirmware *self,
			  GBytes *fw,
			  gsize offset,
			  FwupdInstallFlags flags,
			  GError **error);
	/*< private >*/
	gpointer padding[14];
};

/**
 * FuFmapArea:
 *
 * Specific area of volatile and static regions in firmware binary.
 **/
typedef struct __attribute__((packed)) {
	guint32 offset;			      /* offset relative to base */
	guint32 size;			      /* size in bytes */
	guint8 name[FU_FMAP_FIRMWARE_STRLEN]; /* descriptive name */
	guint16 flags;			      /* flags for this area */
} FuFmapArea;

/**
 * FuFmap:
 *
 * Mapping of volatile and static regions in firmware binary.
 **/
typedef struct __attribute__((packed)) {
	guint8 signature[8];		      /* "__FMAP__" (0x5F5F464D41505F5F) */
	guint8 ver_major;		      /* major version */
	guint8 ver_minor;		      /* minor version */
	guint64 base;			      /* address of the firmware binary */
	guint32 size;			      /* size of firmware binary in bytes */
	guint8 name[FU_FMAP_FIRMWARE_STRLEN]; /* name of this firmware binary */
	guint16 nareas;			      /* number of areas described by
						 areas[] below */
	FuFmapArea areas[];
} FuFmap;

FuFirmware *
fu_fmap_firmware_new(void);
