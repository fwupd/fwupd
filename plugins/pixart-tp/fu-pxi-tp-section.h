/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-fw-struct.h" /* FuPxiTpFirmwareFlag, FuPxiTpUpdateType */
#include "fu-pxi-tp-struct.h"

G_BEGIN_DECLS

#define FU_TYPE_PXI_TP_SECTION (fu_pxi_tp_section_get_type())

G_DECLARE_FINAL_TYPE(FuPxiTpSection, fu_pxi_tp_section, FU, PXI_TP_SECTION, FuFirmware)

/* ctor */
FuPxiTpSection *
fu_pxi_tp_section_new(void) G_GNUC_WARN_UNUSED_RESULT;

/*
 * Decode one FWHD section descriptor into this child image.
 *
 * @buf:   pointer to the section header bytes (starting at the struct)
 * @bufsz: length of @buf; must be >= FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_SIZE
 */
gboolean
fu_pxi_tp_section_process_descriptor(FuPxiTpSection *self,
				     const guint8 *buf,
				     gsize bufsz,
				     GError **error);

FuPxiTpUpdateType
fu_pxi_tp_section_get_update_type(FuPxiTpSection *self);
gboolean
fu_pxi_tp_section_has_flag(FuPxiTpSection *self, FuPxiTpFirmwareFlag flag);
guint32
fu_pxi_tp_section_get_target_flash_start(FuPxiTpSection *self);
guint32
fu_pxi_tp_section_get_section_length(FuPxiTpSection *self);
guint32
fu_pxi_tp_section_get_section_crc(FuPxiTpSection *self);
const guint8 *
fu_pxi_tp_section_get_reserved(FuPxiTpSection *self, gsize *len_out);
GByteArray *
fu_pxi_tp_section_get_payload(FuPxiTpSection *self, GError **error);
gboolean
fu_pxi_tp_section_attach_payload_stream(FuPxiTpSection *self,
					GInputStream *stream,
					gsize file_size,
					GError **error);
G_END_DECLS
