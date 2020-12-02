/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (c) 2020 Synaptics Incorporated.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_SYNAPTICS_RMI_PS2_DEVICE (fu_synaptics_rmi_ps2_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsRmiPs2Device, fu_synaptics_rmi_ps2_device, FU, SYNAPTICS_RMI_PS2_DEVICE, FuUdevDevice)

//FIXME: make upper case?
enum EPS2DataPortCommand {
	edpAuxFullRMIBackDoor		= 0x7F,
	edpAuxAccessModeByte1		= 0xE0,
	edpAuxAccessModeByte2		= 0xE1,
	edpAuxIBMReadSecondaryID	= 0xE1,
	edpAuxSetScaling1To1		= 0xE6,
	edpAuxSetScaling2To1		= 0xE7,
	edpAuxSetResolution		= 0xE8,
	edpAuxStatusRequest		= 0xE9,
	edpAuxSetStreamMode		= 0xEA,
	edpAuxReadData			= 0xEB,
	edpAuxResetWrapMode		= 0xEC,
	edpAuxSetWrapMode		= 0xEE,
	edpAuxSetRemoteMode		= 0xF0,
	edpAuxReadDeviceType		= 0xF2,
	edpAuxSetSampleRate		= 0xF3,
	edpAuxEnable			= 0xF4,
	edpAuxDisable			= 0xF5,
	edpAuxSetDefault		= 0xF6,
	edpAuxResend			= 0xFE,
	edpAuxReset			= 0xFF,
};

enum ESynapticsDeviceResponse {
	esdrTouchPad			= 0x47,
	esdrStyk			= 0x46,
	esdrControlBar			= 0x44,
	esdrRGBControlBar		= 0x43,
};

enum EStatusRequestSequence {
	esrIdentifySynaptics		= 0x00,
	esrReadTouchPadModes		= 0x01,
	esrReadModeByte			= 0x01,
	esrReadEdgeMargins		= 0x02,
	esrReadCapabilities		= 0x02,
	esrReadModelID			= 0x03,
	esrReadCompilationDate		= 0x04,
	esrReadSerialNumberPrefix	= 0x06,
	esrReadSerialNumberSuffix	= 0x07,
	esrReadResolutions		= 0x08,
	esrReadExtraCapabilities1	= 0x09,
	esrReadExtraCapabilities2	= 0x0A,
	esrReadExtraCapabilities3	= 0x0B,
	esrReadExtraCapabilities4	= 0x0C,
	esrReadExtraCapabilities5	= 0x0D,
	esrReadCoordinates		= 0x0D,
	esrReadExtraCapabilities6	= 0x0E,
	esrReadExtraCapabilities7	= 0x0F,
};

enum EPS2DataPortStatus {
	edpsAcknowledge			= 0xFA,
	edpsError			= 0xFC,
	edpsResend			= 0xFE,
	edpsTimeOut			= 0x100
};

enum ESetSampleRateSequence {
	essrSetModeByte1		= 0x0A,
	essrSetModeByte2		= 0x14,
	essrSetModeByte3		= 0x28,
	essrSetModeByte4		= 0x3C,
	essrSetDeluxeModeByte1		= 0x0A,
	essrSetDeluxeModeByte2		= 0x3C,
	essrSetDeluxeModeByte3		= 0xC8,
	essrFastRecalibrate		= 0x50,
	essrPassThroughCommandTunnel	= 0x28
};

enum EDeviceType {
	edtUnknown,
	edtTouchPad,
};

enum EStickDeviceType {
	esdtNone			= 0,
	esdtIBM,
	esdtALPS,
	esdtELAN,
	esdtNXP,
	esdtJYTSyna,
	esdtSynaptics,
	esdtUnknown			= 0xFFFFFFFF
};
