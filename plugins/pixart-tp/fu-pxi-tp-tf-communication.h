/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-register.h"

gboolean
fu_pxi_tp_tf_communication_write_firmware_process(FuPxiTpDevice *self,
						  FuProgress *progress,
						  guint32 send_interval,
						  guint32 data_size,
						  GByteArray *data,
						  const guint8 target_ver[3],
						  GError **error);

gboolean
fu_pxi_tp_tf_communication_exit_upgrade_mode(FuPxiTpDevice *self, GError **error);
