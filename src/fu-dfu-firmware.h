/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_DFU_FIRMWARE (fu_dfu_firmware_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDfuFirmware, fu_dfu_firmware, FU, DFU_FIRMWARE, FuFirmware)

struct _FuDfuFirmwareClass
{
	FuFirmwareClass		 parent_class;
};

FuFirmware		*fu_dfu_firmware_new		(void);
guint16			 fu_dfu_firmware_get_vid	(FuDfuFirmware	*self);
guint16			 fu_dfu_firmware_get_pid	(FuDfuFirmware	*self);
guint16			 fu_dfu_firmware_get_release	(FuDfuFirmware	*self);
guint16			 fu_dfu_firmware_get_version	(FuDfuFirmware	*self);
void			 fu_dfu_firmware_set_vid	(FuDfuFirmware	*self,
							 guint16	 vid);
void			 fu_dfu_firmware_set_pid	(FuDfuFirmware	*self,
							 guint16	 pid);
void			 fu_dfu_firmware_set_release	(FuDfuFirmware	*self,
							 guint16	 release);
void			 fu_dfu_firmware_set_version	(FuDfuFirmware	*self,
							 guint16	 version);
