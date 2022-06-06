/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-dump.h"

/**
 * fu_dump_full:
 * @log_domain: (nullable): optional log domain, typically %G_LOG_DOMAIN
 * @title: (nullable): optional prefix title
 * @data: buffer to print
 * @len: the size of @data
 * @columns: break new lines after this many bytes
 * @flags: dump flags, e.g. %FU_DUMP_FLAGS_SHOW_ASCII
 *
 * Dumps a raw buffer to the screen.
 *
 * Since: 1.8.2
 **/
void
fu_dump_full(const gchar *log_domain,
	     const gchar *title,
	     const guint8 *data,
	     gsize len,
	     guint columns,
	     FuDumpFlags flags)
{
	g_autoptr(GString) str = g_string_new(NULL);

	/* optional */
	if (title != NULL)
		g_string_append_printf(str, "%s:", title);

	/* if more than can fit on one line then start afresh */
	if (len > columns || flags & FU_DUMP_FLAGS_SHOW_ADDRESSES) {
		g_string_append(str, "\n");
	} else {
		for (gsize i = str->len; i < 16; i++)
			g_string_append(str, " ");
	}

	/* offset line */
	if (flags & FU_DUMP_FLAGS_SHOW_ADDRESSES) {
		g_string_append(str, "       │ ");
		for (gsize i = 0; i < columns; i++) {
			g_string_append_printf(str, "%02x ", (guint)i);
			if (flags & FU_DUMP_FLAGS_SHOW_ASCII)
				g_string_append(str, "    ");
		}
		g_string_append(str, "\n───────┼");
		for (gsize i = 0; i < columns; i++) {
			g_string_append(str, "───");
			if (flags & FU_DUMP_FLAGS_SHOW_ASCII)
				g_string_append(str, "────");
		}
		g_string_append_printf(str, "\n0x%04x │ ", (guint)0);
	}

	/* print each row */
	for (gsize i = 0; i < len; i++) {
		g_string_append_printf(str, "%02x ", data[i]);

		/* optionally print ASCII char */
		if (flags & FU_DUMP_FLAGS_SHOW_ASCII) {
			if (g_ascii_isprint(data[i]))
				g_string_append_printf(str, "[%c] ", data[i]);
			else
				g_string_append(str, "[?] ");
		}

		/* new row required */
		if (i > 0 && i != len - 1 && (i + 1) % columns == 0) {
			g_string_append(str, "\n");
			if (flags & FU_DUMP_FLAGS_SHOW_ADDRESSES)
				g_string_append_printf(str, "0x%04x │ ", (guint)i + 1);
		}
	}
	g_log(log_domain, G_LOG_LEVEL_DEBUG, "%s", str->str);
}

/**
 * fu_dump_raw:
 * @log_domain: (nullable): optional log domain, typically %G_LOG_DOMAIN
 * @title: (nullable): optional prefix title
 * @data: buffer to print
 * @len: the size of @data
 *
 * Dumps a raw buffer to the screen.
 *
 * Since: 1.8.2
 **/
void
fu_dump_raw(const gchar *log_domain, const gchar *title, const guint8 *data, gsize len)
{
	FuDumpFlags flags = FU_DUMP_FLAGS_NONE;
	if (len > 64)
		flags |= FU_DUMP_FLAGS_SHOW_ADDRESSES;
	fu_dump_full(log_domain, title, data, len, 32, flags);
}

/**
 * fu_dump_bytes:
 * @log_domain: (nullable): optional log domain, typically %G_LOG_DOMAIN
 * @title: (nullable): optional prefix title
 * @bytes: data blob
 *
 * Dumps a byte buffer to the screen.
 *
 * Since: 1.8.2
 **/
void
fu_dump_bytes(const gchar *log_domain, const gchar *title, GBytes *bytes)
{
	gsize len = 0;
	const guint8 *data = g_bytes_get_data(bytes, &len);
	fu_dump_raw(log_domain, title, data, len);
}
