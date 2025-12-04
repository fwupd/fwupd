#pragma once

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-register.h"

G_BEGIN_DECLS

gboolean
fu_pxi_tp_tf_communication_write_firmware_process(FuDevice *device,
						  FuProgress *progress,
						  guint32 send_interval,
						  guint32 data_size,
						  GByteArray *data,
						  const guint8 target_ver[3],
						  GError **error);

G_END_DECLS
