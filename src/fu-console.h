/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
	FU_CONSOLE_PRINT_FLAG_LIST_ITEM = 1 << 2,
	FU_CONSOLE_PRINT_FLAG_NEWLINE = 1 << 3,
} FuConsolePrintFlags;

gchar *
fu_console_color_format(const gchar *text, FuConsoleColor fg_color) G_GNUC_NON_NULL(1);

FuConsole *
fu_console_new(void);
gboolean
fu_console_setup(FuConsole *self, GError **error) G_GNUC_NON_NULL(1);

guint
fu_console_input_uint(FuConsole *self, guint maxnum, const gchar *format, ...) G_GNUC_PRINTF(3, 4)
    G_GNUC_NON_NULL(1, 3);
gboolean
fu_console_input_bool(FuConsole *self, gboolean def, const gchar *format, ...) G_GNUC_PRINTF(3, 4)
    G_GNUC_NON_NULL(1, 3);

void
fu_console_print_full(FuConsole *self, FuConsolePrintFlags flags, const gchar *format, ...)
    G_GNUC_PRINTF(3, 4) G_GNUC_NON_NULL(1, 3);
void
fu_console_print(FuConsole *self, const gchar *format, ...) G_GNUC_PRINTF(2, 3) G_GNUC_NON_NULL(1);
void
fu_console_print_literal(FuConsole *self, const gchar *text) G_GNUC_NON_NULL(1);
void
fu_console_print_kv(FuConsole *self, const gchar *title, const gchar *msg) G_GNUC_NON_NULL(1);
void
fu_console_line(FuConsole *self, guint width) G_GNUC_NON_NULL(1);
void
fu_console_box(FuConsole *self, const gchar *title, const gchar *body, guint width)
    G_GNUC_NON_NULL(1);
void
fu_console_beep(FuConsole *self, guint count) G_GNUC_NON_NULL(1);

void
fu_console_set_progress(FuConsole *self, FwupdStatus status, guint percentage) G_GNUC_NON_NULL(1);
void
fu_console_set_status_length(FuConsole *self, guint len) G_GNUC_NON_NULL(1);
void
fu_console_set_percentage_length(FuConsole *self, guint len) G_GNUC_NON_NULL(1);
void
fu_console_set_progress_title(FuConsole *self, const gchar *title) G_GNUC_NON_NULL(1);
void
fu_console_set_interactive(FuConsole *self, gboolean interactive) G_GNUC_NON_NULL(1);
void
fu_console_set_main_context(FuConsole *self, GMainContext *main_ctx) G_GNUC_NON_NULL(1);
