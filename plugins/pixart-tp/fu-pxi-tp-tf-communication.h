/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pxi-tp-register.h"

gboolean
fu_pxi_tp_tf_communication_write_firmware_process(FuPxiTpDevice *self,
						  FuProgress *progress,
						  guint32 send_interval,
						  guint32 data_size,
						  GByteArray *data,
						  GError **error);

gboolean
fu_pxi_tp_tf_communication_read_firmware_version(FuPxiTpDevice *self,
						 guint8 mode,
						 guint8 *version,
						 GError **error);

gboolean
fu_pxi_tp_tf_communication_exit_upgrade_mode(FuPxiTpDevice *self, GError **error);
