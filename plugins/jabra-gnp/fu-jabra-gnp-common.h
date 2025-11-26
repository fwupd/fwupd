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

typedef struct {
	guint8 txbuf[FU_JABRA_GNP_BUF_SIZE];
	const guint timeout;
} FuJabraGnpTxData;

typedef struct {
	guint8 rxbuf[FU_JABRA_GNP_BUF_SIZE];
	const guint timeout;
} FuJabraGnpRxData;

typedef struct {
	guint8 major;
	guint8 minor;
	guint8 micro;
} FuJabraGnpVersionData;

guint64
fu_jabra_gnp_calculate_crc(GBytes *bytes);

gboolean
fu_jabra_gnp_ensure_name(FuDevice *self, guint8 address, guint8 seq, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_jabra_gnp_ensure_battery_level(FuDevice *self, guint8 address, guint8 seq, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_jabra_gnp_read_dfu_pid(FuDevice *self,
			  guint8 address,
			  guint8 seq,
			  guint16 *dfu_pid,
			  GError **error) G_GNUC_NON_NULL(1, 4);
gboolean
fu_jabra_gnp_ensure_version(FuDevice *self, guint8 address, guint8 seq, GError **error)
    G_GNUC_NON_NULL(1);

gboolean
fu_jabra_gnp_read_fwu_protocol(FuDevice *self,
			       guint8 address,
			       guint8 seq,
			       guint8 *fwu_protocol,
			       GError **error) G_GNUC_NON_NULL(1, 4);
gboolean
fu_jabra_gnp_write_partition(FuDevice *self,
			     guint8 address,
			     guint8 seq,
			     guint8 part,
			     GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_jabra_gnp_start(FuDevice *self, guint8 address, guint8 seq, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_jabra_gnp_flash_erase_done(FuDevice *self, guint8 address, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_jabra_gnp_write_crc(FuDevice *self,
		       guint8 address,
		       guint8 seq,
		       guint32 crc,
		       guint total_chunks,
		       guint preload_count,
		       GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_jabra_gnp_write_extended_crc(FuDevice *self,
				guint8 address,
				guint8 seq,
				guint32 crc,
				guint total_chunks,
				guint preload_count,
				GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_jabra_gnp_write_chunks(FuDevice *self,
			  guint8 address,
			  FuChunkArray *chunks,
			  FuProgress *progress,
			  GError **error) G_GNUC_NON_NULL(1, 3);
gboolean
fu_jabra_gnp_read_verify_status(FuDevice *self, guint8 address, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_jabra_gnp_write_version(FuDevice *self,
			   guint8 address,
			   guint8 seq,
			   FuJabraGnpVersionData *version_data,
			   GError **error) G_GNUC_NON_NULL(1, 4);
gboolean
fu_jabra_gnp_write_dfu_from_squif(FuDevice *self, guint8 address, guint8 seq, GError **error)
    G_GNUC_NON_NULL(1);
