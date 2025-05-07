/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_JABRA_GNP_BUF_SIZE			64
#define FU_JABRA_GNP_MAX_RETRIES		3
#define FU_JABRA_GNP_PRELOAD_COUNT		10
#define FU_JABRA_GNP_RETRY_DELAY		100   /* ms */
#define FU_JABRA_GNP_STANDARD_SEND_TIMEOUT	3000  /* ms */
#define FU_JABRA_GNP_STANDARD_RECEIVE_TIMEOUT	1000  /* ms */
#define FU_JABRA_GNP_LONG_RECEIVE_TIMEOUT	30000 /* ms */
#define FU_JABRA_GNP_EXTRA_LONG_RECEIVE_TIMEOUT 60000 /* ms */

#define FU_JABRA_GNP_IFACE 0x05

#define FU_JABRA_GNP_ADDRESS_PARENT    0x01
#define FU_JABRA_GNP_ADDRESS_OTA_CHILD 0x04

#define FU_JABRA_GNP_PROTOCOL_OTA	   7
#define FU_JABRA_GNP_PROTOCOL_EXTENDED_OTA 16

struct _FuJabraGnpDevice {
	FuUsbDevice parent_instance;
	guint8 fwu_protocol;
	guint8 iface_hid;
	guint8 sequence_number;
	guint8 address;
	guint8 epin;
	guint dfu_pid;
};

typedef struct {
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE];
	const guint timeout;
} FuJabraGnpTxData;

typedef struct {
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE];
	const guint timeout;
} FuJabraGnpRxData;

#define FU_TYPE_JABRA_GNP_DEVICE (fu_jabra_gnp_device_get_type())
G_DECLARE_FINAL_TYPE(FuJabraGnpDevice, fu_jabra_gnp_device, FU, JABRA_GNP_DEVICE, FuUsbDevice)
