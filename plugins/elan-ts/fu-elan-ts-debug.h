/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
 
#pragma once

/**
 * ELAN_TS_DEBUG:
 * @debug_setting: debug setting bitmask from the firmware header
 * @fmt: printf-style format string
 * @...: arguments for @fmt
 *
 * Logs a message. If %FU_ELAN_TS_DEBUG_SETTING_ENABLE_DEBUG_MSG is set,
 * the message is promoted to g_message() with a monotonic microsecond timestamp.
 **/
#define ELAN_TS_DEBUG(debug_setting, fmt, ...)                                   \
	do {                                                                     \
		if (((debug_setting) & FU_ELAN_TS_DEBUG_SETTING_ENABLE_DEBUG_MSG) != 0) { \
			g_message("[time_offset_us: %" G_GINT64_FORMAT "] " fmt,                      \
				  (gint64)g_get_monotonic_time(), ##__VA_ARGS__);        \
		} else {                                                         \
			g_debug(fmt, ##__VA_ARGS__);                             \
		}                                                                \
	} while (0)

/**
 * ELAN_TS_ERROR:
 * @debug_setting: bitmask to control log promotion
 * @error: a #GError double pointer (GError **)
 * @fmt: printf-style format string for the prefix
 * @...: arguments for @fmt
 *
 * Prefixes the error and optionally promotes it to g_message() with a timestamp.
 */
#define ELAN_TS_ERROR(debug_setting, error, fmt, ...)                            \
	do {                                                                     \
		if ((error != NULL) && (*error != NULL))                             \
			g_prefix_error(error, fmt, ##__VA_ARGS__);               \
		if (((debug_setting) & FU_ELAN_TS_DEBUG_SETTING_ENABLE_DEBUG_MSG) != 0) { \
			g_message("[time_offset_us: %" G_GINT64_FORMAT "] " fmt,                      \
				  (gint64)g_get_monotonic_time(), ##__VA_ARGS__);        \
		}                                                                \
	} while (0)

/**
 * ELAN_TS_SET_ERROR:
 * @debug_setting: bitmask
 * @error: a #GError double pointer (GError **)
 * @code: #FwupdError code
 * @fmt: printf-style format string
 * @...: arguments for @fmt
 *
 * Sets the GError and promotes to g_message() with a timestamp if needed.
 */
#define ELAN_TS_SET_ERROR(debug_setting, error, code, fmt, ...)                  \
	do {                                                                     \
		g_set_error(error, FWUPD_ERROR, code, fmt, ##__VA_ARGS__);       \
		if (((debug_setting) & FU_ELAN_TS_DEBUG_SETTING_ENABLE_DEBUG_MSG) != 0) { \
			g_message("[time_offset_us: %" G_GINT64_FORMAT "] " fmt,                      \
				  (gint64)g_get_monotonic_time(), ##__VA_ARGS__);        \
		}                                                                \
	} while (0)
