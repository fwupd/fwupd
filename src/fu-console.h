/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CONSOLE (fu_console_get_type())
G_DECLARE_FINAL_TYPE(FuConsole, fu_console, FU, CONSOLE, GObject)

typedef enum {
	FU_CONSOLE_COLOR_BLACK = 30,
	FU_CONSOLE_COLOR_RED = 31,
	FU_CONSOLE_COLOR_GREEN = 32,
	FU_CONSOLE_COLOR_YELLOW = 33,
	FU_CONSOLE_COLOR_BLUE = 34,
	FU_CONSOLE_COLOR_MAGENTA = 35,
	FU_CONSOLE_COLOR_CYAN = 36,
	FU_CONSOLE_COLOR_WHITE = 37,
} FuConsoleColor;

typedef enum {
	FU_CONSOLE_PRINT_FLAG_NONE = 0,
	FU_CONSOLE_PRINT_FLAG_STDERR = 1 << 0,
	FU_CONSOLE_PRINT_FLAG_WARNING = 1 << 1,
} FuConsolePrintFlags;

gchar *
fu_console_color_format(const gchar *text, FuConsoleColor fg_color);

FuConsole *
fu_console_new(void);
gboolean
fu_console_setup(FuConsole *self, GError **error);

guint
fu_console_input_uint(FuConsole *self, guint maxnum, const gchar *format, ...) G_GNUC_PRINTF(3, 4);
gboolean
fu_console_input_bool(FuConsole *self, gboolean def, const gchar *format, ...) G_GNUC_PRINTF(3, 4);

void
fu_console_print_full(FuConsole *self, FuConsolePrintFlags flags, const gchar *format, ...)
    G_GNUC_PRINTF(3, 4);
void
fu_console_print(FuConsole *self, const gchar *format, ...) G_GNUC_PRINTF(2, 3);
void
fu_console_print_literal(FuConsole *self, const gchar *text);
void
fu_console_print_kv(FuConsole *self, const gchar *title, const gchar *msg);
void
fu_console_line(FuConsole *self, guint width);
void
fu_console_box(FuConsole *self, const gchar *title, const gchar *body, guint width);

void
fu_console_set_progress(FuConsole *self, FwupdStatus status, guint percentage);
void
fu_console_set_status_length(FuConsole *self, guint len);
void
fu_console_set_percentage_length(FuConsole *self, guint len);
void
fu_console_set_progress_title(FuConsole *self, const gchar *title);
void
fu_console_set_interactive(FuConsole *self, gboolean interactive);
void
fu_console_set_main_context(FuConsole *self, GMainContext *main_ctx);
