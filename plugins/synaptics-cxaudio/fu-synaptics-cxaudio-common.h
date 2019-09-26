/*
 * Copyright (C) 2005-2019 Synaptics Incorporated
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

/* usb */
#define FU_SYNAPTICS_CXAUDIO_INPUT_REPORT_SIZE			35
#define FU_SYNAPTICS_CXAUDIO_OUTPUT_REPORT_SIZE			39
#define FU_SYNAPTICS_CXAUDIO_HID_INTERFACE			0x03
#define FU_SYNAPTICS_CXAUDIO_USB_TIMEOUT			2000 /* ms */

/* commands */
#define FU_SYNAPTICS_CXAUDIO_MEM_WRITEID			0x4
#define FU_SYNAPTICS_CXAUDIO_MEM_READID				0x5

typedef enum {
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_UNKNOWN,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_CX20562		= 20562,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_CX2070x		= 20700,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_CX2077x		= 20770,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_CX2076x		= 20760,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_CX2085x		= 20850,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_CX2089x		= 20890,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_CX2098x		= 20980,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_CX2198x		= 21980,
	FU_SYNAPTICS_CXAUDIO_DEVICE_KIND_LAST
} FuSynapticsCxaudioDeviceKind;

typedef enum {
	FU_SYNAPTICS_CXAUDIO_MEM_KIND_EEPROM,
	FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_RAM,
	FU_SYNAPTICS_CXAUDIO_MEM_KIND_CPX_ROM,
	FU_SYNAPTICS_CXAUDIO_MEM_KIND_LAST
} FuSynapticsCxaudioMemKind;

/* EEPROM */
#define FU_SYNAPTICS_CXAUDIO_EEPROM_VALIDITY_SIGNATURE_OFFSET	0x0000
#define FU_SYNAPTICS_CXAUDIO_EEPROM_CUSTOM_INFO_OFFSET		0x0020
#define FU_SYNAPTICS_CXAUDIO_EEPROM_CPX_PATCH_VERSION_ADDRESS	0x0022
#define FU_SYNAPTICS_CXAUDIO_EEPROM_CPX_PATCH2_VERSION_ADDRESS	0x0176
#define FU_SYNAPTICS_CXAUDIO_EEPROM_STORAGE_SIZE_ADDRESS	0x0005
#define FU_SYNAPTICS_CXAUDIO_EEPROM_STORAGE_PADDING_SIZE	0x4 /* bytes */

#define FU_SYNAPTICS_CXAUDIO_DEVICE_CAPABILITIES_STRIDX		50
#define FU_SYNAPTICS_CXAUDIO_DEVICE_CAPABILITIES_BYTE		0x03
#define FU_SYNAPTICS_CXAUDIO_MAGIC_BYTE				'L'
#define FU_SYNAPTICS_CXAUDIO_SIGNATURE_BYTE			'S'
#define FU_SYNAPTICS_CXAUDIO_SIGNATURE_PATCH_BYTE		'P'
#define FU_SYNAPTICS_CXAUDIO_REG_FIRMWARE_PARK_ADDR		0x1000
#define FU_SYNAPTICS_CXAUDIO_REG_FIRMWARE_VERSION_ADDR		0x1001
#define FU_SYNAPTICS_CXAUDIO_REG_RESET_ADDR			0x0400
#define FU_SYNAPTICS_CXAUDIO_EEPROM_SHADOW_SIZE			(8 * 1024)

typedef guint16 FuSynapticsCxaudioEepromPtr;
typedef struct __attribute__ ((packed)) {
	FuSynapticsCxaudioEepromPtr	 PatchVersionStringAddress;
	guint8				 CpxPatchVersion[3];
	guint8				 SpxPatchVersion[4];
	guint8				 LayoutSignature;
	guint8				 LayoutVersion;
	guint8				 ApplicationStatus;
	guint16				 VendorID;
	guint16				 ProductID;
	guint16				 RevisionID;
	FuSynapticsCxaudioEepromPtr	 LanguageStringAddress;
	FuSynapticsCxaudioEepromPtr	 ManufacturerStringAddress;
	FuSynapticsCxaudioEepromPtr	 ProductStringAddress;
	FuSynapticsCxaudioEepromPtr	 SerialNumberStringAddress;
} FuSynapticsCxaudioEepromCustomInfo;

#define FU_SYNAPTICS_CXAUDIO_EEPROM_APP_STATUS_ADDRESS		(FU_SYNAPTICS_CXAUDIO_EEPROM_CUSTOM_INFO_OFFSET + G_STRUCT_OFFSET(FuSynapticsCxaudioEepromCustomInfo, ApplicationStatus))
#define FU_SYNAPTICS_CXAUDIO_EEPROM_LAYOUT_SIGNATURE_ADDRESS	(FU_SYNAPTICS_CXAUDIO_EEPROM_CUSTOM_INFO_OFFSET + G_STRUCT_OFFSET(FuSynapticsCxaudioEepromCustomInfo, LayoutSignature))
#define FU_SYNAPTICS_CXAUDIO_EEPROM_LAYOUT_VERSION_ADDRESS	(FU_SYNAPTICS_CXAUDIO_EEPROM_CUSTOM_INFO_OFFSET + G_STRUCT_OFFSET(FuSynapticsCxaudioEepromCustomInfo, LayoutVersion))

typedef struct __attribute__ ((packed)) {
	guint8			 Length;
	guint8			 Type;
} FuSynapticsCxaudioEepromStringHeader;

typedef struct __attribute__ ((packed)) {
	guint8			 PatchSignature;
	FuSynapticsCxaudioEepromPtr	 PatchAddress;
} FuSynapticsCxaudioEepromPatchInfo;

typedef struct __attribute__ ((packed)) {
	guint8			 MagicByte;
	guint8			 EeepromSizeCode;
} FuSynapticsCxaudioEepromValiditySignature;

#define FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_INFO_OFFSET		0x0014
#define FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_INFO_SIZE		(sizeof(FuSynapticsCxaudioEepromPatchInfo))
#define FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_SIGNATURE_ADDRESS	(FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_INFO_OFFSET + G_STRUCT_OFFSET(FuSynapticsCxaudioEepromPatchInfo, PatchSignature))
#define FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_PTR_ADDRESS		(FU_SYNAPTICS_CXAUDIO_EEPROM_PATCH_INFO_OFFSET + G_STRUCT_OFFSET(FuSynapticsCxaudioEepromPatchInfo, PatchAddress))
#define FU_SYNAPTICS_CXAUDIO_FIRMWARE_SIGNATURE_OFFSET		(FU_SYNAPTICS_CXAUDIO_EEPROM_VALIDITY_SIGNATURE_OFFSET + sizeof(FuSynapticsCxaudioEepromValiditySignature))
