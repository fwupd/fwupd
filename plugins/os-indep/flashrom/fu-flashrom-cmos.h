/*
 * Copyright 2021 Sean Rhodes <sean@starlabs.systems>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

/* From coreboot's src/include/pc80/mc146818rtc.h file */
#define RTC_BASE_PORT 0x70

/*
 * This is the offset of the first of the two checksum bytes
 * we may want to figure out how we can determine this dynamically
 * during execution.
 */
#define CMOS_CHECKSUM_OFFSET 123

gboolean
fu_flashrom_cmos_reset(GError **error);
