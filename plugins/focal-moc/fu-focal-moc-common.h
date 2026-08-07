/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-focal-moc-struct.h"

#define FU_FOCAL_MOC_YMODEM_DATA_SIZE 1024
#define FU_FOCAL_MOC_VERSION_MAX_SIZE 64

GByteArray *
fu_focal_moc_packet_new(guint8 command, const guint8 *data, gsize data_sz, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;

GByteArray *
fu_focal_moc_packet_parse(const guint8 *buf,
			  gsize bufsz,
			  gboolean allow_legacy_trailer,
			  guint8 *status,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;

GByteArray *
fu_focal_moc_ymodem_build_soh(guint protocol_version,
			      gsize firmware_sz,
			      guint32 crc32,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT;

GByteArray *
fu_focal_moc_ymodem_build_data(guint protocol_version,
			       FuFocalMocFrame kind,
			       guint16 sequence,
			       const guint8 *data,
			       gsize data_sz,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT;

gchar *
fu_focal_moc_version_parse(const guint8 *buf, gsize bufsz, gboolean *is_bootloader, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;

gboolean
fu_focal_moc_fp_version_validate(const guint8 *buf,
				 gsize bufsz,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;

gboolean
fu_focal_moc_iap_probe_required(gboolean is_bootloader, gboolean is_protocol_v2);

gboolean
fu_focal_moc_iap_status_layout_from_probe(guint8 status,
					  const guint8 *buf,
					  gsize bufsz,
					  FuFocalMocIapStatusLayout *layout,
					  GError **error) G_GNUC_WARN_UNUSED_RESULT;

gboolean
fu_focal_moc_status_to_error(guint8 status,
			     const gchar *context,
			     FwupdError invalid_command_code,
			     gboolean status_aligned,
			     GByteArray *data,
			     GError **error);
