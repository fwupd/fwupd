/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuProgressBar"

#include "config.h"

#include <glib/gi18n.h>
#include <stdio.h>

#include "fu-console.h"

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif

struct _FuConsole {
	GObject parent_instance;
	GMainContext *main_ctx;
	FwupdStatus status;
	gboolean spinner_count_up; /* width in visible chars */
	guint spinner_idx;	   /* width in visible chars */
	guint length_percentage;   /* width in visible chars */
	guint length_status;	   /* width in visible chars */
	guint percentage;
	GSource *timer_source;
	gint64 last_animated; /* monotonic */
	GTimer *time_elapsed;
	gdouble last_estimate;
	gboolean interactive;
	gboolean contents_to_clear;
};

G_DEFINE_TYPE(FuConsole, fu_console, G_TYPE_OBJECT)

gboolean
fu_console_setup(FuConsole *self, GError **error)
{
#ifdef _WIN32
	HANDLE hOut;
	DWORD dwMode = 0;

	/* enable VT sequences */
	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to get stdout [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	if (!GetConsoleMode(hOut, &dwMode)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to get mode [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hOut, dwMode)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to set mode [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	if (!SetConsoleOutputCP(CP_UTF8)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to set output UTF-8 [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
	if (!SetConsoleCP(CP_UTF8)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to set UTF-8 [%u]",
			    (guint)GetLastError());
		return FALSE;
	}
#else
	if (isatty(fileno(stdout)) == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "not a TTY");
		return FALSE;
	}
#endif
	/* success */
	return TRUE;
}

static void
fu_console_erase_line(FuConsole *self)
{
	if (!self->interactive)
		return;
	g_print("\033[G");
}

static void
fu_console_reset_line(FuConsole *self)
{
	if (self->contents_to_clear) {
		fu_console_erase_line(self);
		g_print("\n");
		self->contents_to_clear = FALSE;
	}
}

void
fu_console_print_kv(FuConsole *self, const gchar *title, const gchar *msg)
{
	gsize title_len;
	g_auto(GStrv) lines = NULL;

	if (msg == NULL)
		return;
	fu_console_reset_line(self);
	g_print("%s:", title);

	/* pad */
	title_len = fu_strwidth(title) + 1;
	lines = g_strsplit(msg, "\n", -1);
	for (guint j = 0; lines[j] != NULL; j++) {
		for (gsize i = title_len; i < 25; i++)
			g_print(" ");
		g_print("%s\n", lines[j]);
		title_len = 0;
	}
}

guint
fu_console_input_uint(FuConsole *self, guint maxnum, const gchar *format, ...)
{
	gint retval;
	guint answer = 0;
	va_list args;
	g_autofree gchar *tmp = NULL;

	va_start(args, format);
	tmp = g_strdup_vprintf(format, args);
	va_end(args);

	fu_console_print_full(self, FU_CONSOLE_PRINT_FLAG_NONE, "%s [0-%u]: ", tmp, maxnum);
	do {
		char buffer[64];

		/* swallow the \n at end of line too */
		if (!fgets(buffer, sizeof(buffer), stdin))
			break;
		if (strlen(buffer) == sizeof(buffer) - 1)
			continue;

		/* get a number */
		retval = sscanf(buffer, "%u", &answer);

		/* positive */
		if (retval == 1 && answer <= maxnum)
			break;

		/* TRANSLATORS: the user isn't reading the question */
		fu_console_print_full(self,
				      FU_CONSOLE_PRINT_FLAG_NONE,
				      _("Please enter a number from 0 to %u: "),
				      maxnum);
	} while (TRUE);
	return answer;
}

gboolean
fu_console_input_bool(FuConsole *self, gboolean def, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *tmp = NULL;

	va_start(args, format);
	tmp = g_strdup_vprintf(format, args);
	va_end(args);

	fu_console_print_full(self,
			      FU_CONSOLE_PRINT_FLAG_NONE,
			      "%s [%s]: ",
			      tmp,
			      def ? "Y|n" : "y|N");
	do {
		char buffer[4];
		if (!fgets(buffer, sizeof(buffer), stdin))
			continue;
		if (strlen(buffer) == sizeof(buffer) - 1)
			continue;
		if (g_strcmp0(buffer, "\n") == 0)
			return def;
		buffer[0] = g_ascii_toupper(buffer[0]);
		if (g_strcmp0(buffer, "Y\n") == 0)
			return TRUE;
		if (g_strcmp0(buffer, "N\n") == 0)
			return FALSE;
	} while (TRUE);
	return FALSE;
}

static GPtrArray *
fu_console_strsplit_words(const gchar *text, guint line_len)
{
	g_auto(GStrv) tokens = NULL;
	g_autoptr(GPtrArray) lines = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GString) curline = g_string_new(NULL);

	/* sanity check */
	if (text == NULL || text[0] == '\0')
		return NULL;
	if (line_len == 0)
		return NULL;

	/* tokenize the string */
	tokens = g_strsplit(text, " ", -1);
	for (guint i = 0; tokens[i] != NULL; i++) {
		/* current line plus new token is okay */
		if (curline->len + fu_strwidth(tokens[i]) < line_len) {
			g_string_append_printf(curline, "%s ", tokens[i]);
			continue;
		}

		/* too long, so remove space, add newline and dump */
		if (curline->len > 0)
			g_string_truncate(curline, curline->len - 1);
		g_ptr_array_add(lines, g_strdup(curline->str));
		g_string_truncate(curline, 0);
		g_string_append_printf(curline, "%s ", tokens[i]);
	}

	/* any incomplete line? */
	if (curline->len > 0) {
		g_string_truncate(curline, curline->len - 1);
		g_ptr_array_add(lines, g_strdup(curline->str));
	}
	return g_steal_pointer(&lines);
}

static void
fu_console_box_line(const gchar *start,
		    const gchar *text,
		    const gchar *end,
		    const gchar *padding,
		    guint width)
{
	guint offset = 0;
	if (start != NULL) {
		offset += fu_strwidth(start);
		g_print("%s", start);
	}
	if (text != NULL) {
		offset += fu_strwidth(text);
		g_print("%s", text);
	}
	if (end != NULL)
		offset += fu_strwidth(end);
	for (guint i = offset; i < width; i++)
		g_print("%s", padding);
	if (end != NULL)
		g_print("%s\n", end);
}

void
fu_console_line(FuConsole *self, guint width)
{
	g_autoptr(GString) str = g_string_new_len(NULL, width);
	for (guint i = 0; i < width; i++)
		g_string_append(str, "─");
	fu_console_print_literal(self, str->str);
}

void
fu_console_box(FuConsole *self, const gchar *title, const gchar *body, guint width)
{
	/* nothing to do */
	if (title == NULL && body == NULL)
		return;

	/* header */
	fu_console_reset_line(self);
	fu_console_box_line("╔", NULL, "╗", "═", width);

	/* optional title */
	if (title != NULL) {
		g_autoptr(GPtrArray) lines = fu_console_strsplit_words(title, width - 4);
		for (guint j = 0; j < lines->len; j++) {
			const gchar *line = g_ptr_array_index(lines, j);
			fu_console_box_line("║ ", line, " ║", " ", width);
		}
	}

	/* join */
	if (title != NULL && body != NULL)
		fu_console_box_line("╠", NULL, "╣", "═", width);

	/* optional body */
	if (body != NULL) {
		gboolean has_nonempty = FALSE;
		g_auto(GStrv) split = g_strsplit(body, "\n", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			g_autoptr(GPtrArray) lines = fu_console_strsplit_words(split[i], width - 4);
			if (lines == NULL) {
				if (has_nonempty) {
					fu_console_box_line("║ ", NULL, " ║", " ", width);
					has_nonempty = FALSE;
				}
				continue;
			}
			for (guint j = 0; j < lines->len; j++) {
				const gchar *line = g_ptr_array_index(lines, j);
				fu_console_box_line("║ ", line, " ║", " ", width);
			}
			has_nonempty = TRUE;
		}
	}

	/* footer */
	fu_console_box_line("╚", NULL, "╝", "═", width);
}

static const gchar *
fu_console_status_to_string(FwupdStatus status)
{
	switch (status) {
	case FWUPD_STATUS_IDLE:
		/* TRANSLATORS: daemon is inactive */
		return _("Idle…");
		break;
	case FWUPD_STATUS_DECOMPRESSING:
		/* TRANSLATORS: decompressing the firmware file */
		return _("Decompressing…");
		break;
	case FWUPD_STATUS_LOADING:
		/* TRANSLATORS: parsing the firmware information */
		return _("Loading…");
		break;
	case FWUPD_STATUS_DEVICE_RESTART:
		/* TRANSLATORS: restarting the device to pick up new F/W */
		return _("Restarting device…");
		break;
	case FWUPD_STATUS_DEVICE_READ:
		/* TRANSLATORS: reading from the flash chips */
		return _("Reading…");
		break;
	case FWUPD_STATUS_DEVICE_WRITE:
		/* TRANSLATORS: writing to the flash chips */
		return _("Writing…");
		break;
	case FWUPD_STATUS_DEVICE_ERASE:
		/* TRANSLATORS: erasing contents of the flash chips */
		return _("Erasing…");
		break;
	case FWUPD_STATUS_DEVICE_VERIFY:
		/* TRANSLATORS: verifying we wrote the firmware correctly */
		return _("Verifying…");
		break;
	case FWUPD_STATUS_SCHEDULING:
		/* TRANSLATORS: scheduling an update to be done on the next boot */
		return _("Scheduling…");
		break;
	case FWUPD_STATUS_DOWNLOADING:
		/* TRANSLATORS: downloading from a remote server */
		return _("Downloading…");
		break;
	case FWUPD_STATUS_WAITING_FOR_AUTH:
		/* TRANSLATORS: waiting for user to authenticate */
		return _("Authenticating…");
		break;
	case FWUPD_STATUS_DEVICE_BUSY:
		/* TRANSLATORS: waiting for device to do something */
		return _("Waiting…");
		break;
	default:
		break;
	}

	/* TRANSLATORS: current daemon status is unknown */
	return _("Unknown");
}

static gboolean
_fu_status_is_predictable(FwupdStatus status)
{
	if (status == FWUPD_STATUS_DEVICE_ERASE)
		return TRUE;
	if (status == FWUPD_STATUS_DEVICE_VERIFY)
		return TRUE;
	if (status == FWUPD_STATUS_DEVICE_READ)
		return TRUE;
	if (status == FWUPD_STATUS_DEVICE_WRITE)
		return TRUE;
	if (status == FWUPD_STATUS_DOWNLOADING)
		return TRUE;
	return FALSE;
}

static gboolean
fu_console_estimate_ready(FuConsole *self, guint percentage)
{
	gdouble old;
	gdouble elapsed;

	/* now invalid */
	if (percentage == 0 || percentage == 100) {
		g_timer_start(self->time_elapsed);
		self->last_estimate = 0;
		return FALSE;
	}

	/* allow-list things that make sense... */
	if (!_fu_status_is_predictable(self->status))
		return FALSE;

	old = self->last_estimate;
	elapsed = g_timer_elapsed(self->time_elapsed, NULL);
	self->last_estimate = elapsed / percentage * (100 - percentage);

	/* estimate is ready if we have decreased */
	return old > self->last_estimate;
}

static gchar *
fu_console_time_remaining_str(FuConsole *self)
{
	/* less than 5 seconds remaining */
	if (self->last_estimate < 5)
		return NULL;

	/* less than 60 seconds remaining */
	if (self->last_estimate < 60) {
		/* TRANSLATORS: time remaining for completing firmware flash */
		return g_strdup(_("Less than one minute remaining"));
	}

	return g_strdup_printf(
	    /* TRANSLATORS: more than a minute */
	    ngettext("%.0f minute remaining", "%.0f minutes remaining", self->last_estimate / 60),
	    self->last_estimate / 60);
}

static void
fu_console_refresh(FuConsole *self)
{
	const gchar *title;
	guint i;
	g_autoptr(GString) str = g_string_new(NULL);

	/* sanity check */
	if (self->status == FWUPD_STATUS_IDLE || self->status == FWUPD_STATUS_UNKNOWN)
		return;

	/* erase previous line */
	fu_console_erase_line(self);

	/* add status */
	title = fu_console_status_to_string(self->status);
	g_string_append(str, title);
	for (i = fu_strwidth(str->str); i < self->length_status; i++)
		g_string_append_c(str, ' ');

	/* add console */
	g_string_append(str, "[");
	if (self->percentage > 0) {
		for (i = 0; i < (self->length_percentage - 1) * self->percentage / 100; i++)
			g_string_append_c(str, '*');
		for (i = i + 1; i < self->length_percentage; i++)
			g_string_append_c(str, ' ');
	} else {
		const gchar chars[] = {
		    '-',
		    '\\',
		    '|',
		    '/',
		};
		for (i = 0; i < self->spinner_idx; i++)
			g_string_append_c(str, ' ');
		g_string_append_c(str, chars[i / 4 % G_N_ELEMENTS(chars)]);
		for (i = i + 1; i < self->length_percentage - 1; i++)
			g_string_append_c(str, ' ');
	}
	g_string_append_c(str, ']');

	/* once we have good data show an estimate of time remaining */
	if (fu_console_estimate_ready(self, self->percentage)) {
		g_autofree gchar *remaining = fu_console_time_remaining_str(self);
		if (remaining != NULL)
			g_string_append_printf(str, " %s…", remaining);
	}

	/* dump to screen */
	g_print("%s", str->str);
	self->contents_to_clear = TRUE;
}

/**
 * fu_console_print_full:
 * @self: a #FuConsole
 * @flags; a #FuConsolePrintFlags, e.g. %FU_CONSOLE_PRINT_FLAG_STDERR
 * @text: string
 *
 * Clears the console, and prints the text.
 **/
void
fu_console_print_full(FuConsole *self, FuConsolePrintFlags flags, const gchar *format, ...)
{
	va_list args;
	g_autoptr(GString) str = g_string_new(NULL);

	va_start(args, format);
	g_string_append_vprintf(str, format, args);
	va_end(args);

	if (flags & FU_CONSOLE_PRINT_FLAG_WARNING) {
		/* TRANSLATORS: this is a prefix on the console */
		g_autofree gchar *fmt = fu_console_color_format(_("WARNING"), FU_CONSOLE_COLOR_RED);
		g_string_prepend(str, ": ");
		g_string_prepend(str, fmt);
		flags |= FU_CONSOLE_PRINT_FLAG_STDERR;
	}

	fu_console_reset_line(self);
	if (flags & FU_CONSOLE_PRINT_FLAG_STDERR) {
		g_printerr("%s", str->str);
	} else {
		g_print("%s", str->str);
	}
}

void
fu_console_print_literal(FuConsole *self, const gchar *text)
{
	fu_console_reset_line(self);
	g_print("%s\n", text);
}

/**
 * fu_console_print:
 * @self: a #FuConsole
 * @text: string
 *
 * Clears the console, prints the text and prints a newline.
 **/
void
fu_console_print(FuConsole *self, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *tmp = NULL;

	va_start(args, format);
	tmp = g_strdup_vprintf(format, args);
	va_end(args);
	fu_console_print_literal(self, tmp);
}

/**
 * fu_console_set_progress_title:
 * @self: A #FuConsole
 * @title: A string
 *
 * Sets console title
 **/
void
fu_console_set_progress_title(FuConsole *self, const gchar *title)
{
	fu_console_erase_line(self);
	g_print("%s\n", title);
	fu_console_refresh(self);
}

/**
 * fu_console_set_main_context:
 * @self: A #FuConsole
 * @main_ctx: (nullable): main context
 *
 * Sets console main context to use for animations.
 **/
void
fu_console_set_main_context(FuConsole *self, GMainContext *main_ctx)
{
	self->main_ctx = g_main_context_ref(main_ctx);
}

static void
fu_console_spin_inc(FuConsole *self)
{
	/* reset */
	self->last_animated = g_get_monotonic_time();

	/* up to down */
	if (self->spinner_count_up) {
		if (++self->spinner_idx > self->length_percentage - 3)
			self->spinner_count_up = FALSE;
	} else {
		if (--self->spinner_idx == 0)
			self->spinner_count_up = TRUE;
	}
}

static gboolean
fu_console_spin_cb(gpointer user_data)
{
	FuConsole *self = FU_CONSOLE(user_data);

	/* move the spinner index up to down */
	fu_console_spin_inc(self);

	/* update the terminal */
	fu_console_refresh(self);

	return G_SOURCE_CONTINUE;
}

static void
fu_console_spin_end(FuConsole *self)
{
	if (self->timer_source != NULL) {
		g_source_destroy(self->timer_source);
		self->timer_source = NULL;

		/* reset when the spinner has been stopped */
		g_timer_start(self->time_elapsed);
	}

	/* go back to the start when we next go into unknown percentage mode */
	self->spinner_idx = 0;
	self->spinner_count_up = TRUE;
}

static void
fu_console_spin_start(FuConsole *self)
{
	if (self->timer_source != NULL)
		g_source_destroy(self->timer_source);
	self->timer_source = g_timeout_source_new(40);
	g_source_set_callback(self->timer_source, fu_console_spin_cb, self, NULL);
	g_source_attach(self->timer_source, self->main_ctx);
}

/**
 * fu_console_set_progress:
 * @self: A #FuConsole
 * @status: A #FwupdStatus
 * @percentage: unsigned integer
 *
 * Refreshes the progress bar with the new percentage and status.
 **/
void
fu_console_set_progress(FuConsole *self, FwupdStatus status, guint percentage)
{
	g_return_if_fail(FU_IS_CONSOLE(self));

	/* not useful */
	if (status == FWUPD_STATUS_UNKNOWN)
		return;

	/* ignore duplicates */
	if (self->status == status && self->percentage == percentage)
		return;

	/* cache */
	self->status = status;
	self->percentage = percentage;

	/* dumb */
	if (!self->interactive && percentage != 0 && status != FWUPD_STATUS_IDLE) {
		g_printerr("%s: %u%%\n", fu_console_status_to_string(status), percentage);
		return;
	}

	/* if the main loop isn't spinning and we've not had a chance to
	 * execute the callback just do the refresh now manually */
	if (percentage == 0 && status != FWUPD_STATUS_IDLE &&
	    self->status != FWUPD_STATUS_UNKNOWN) {
		if ((g_get_monotonic_time() - self->last_animated) / 1000 > 40) {
			fu_console_spin_inc(self);
			fu_console_refresh(self);
		}
	}

	/* enable or disable the spinner timeout */
	if (percentage > 0) {
		fu_console_spin_end(self);
	} else {
		fu_console_spin_start(self);
	}

	/* update the terminal */
	fu_console_refresh(self);
}

/**
 * fu_console_set_interactive:
 * @self: A #FuConsole
 * @interactive: #gboolean
 *
 * Marks the console as interactive or not
 **/
void
fu_console_set_interactive(FuConsole *self, gboolean interactive)
{
	g_return_if_fail(FU_IS_CONSOLE(self));
	self->interactive = interactive;
}

/**
 * fu_console_set_status_length:
 * @self: A #FuConsole
 * @len: unsigned integer
 *
 * Sets the width of the progressbar status, which must be greater that 3.
 **/
void
fu_console_set_status_length(FuConsole *self, guint len)
{
	g_return_if_fail(FU_IS_CONSOLE(self));
	g_return_if_fail(len > 3);
	self->length_status = len;
}

/**
 * fu_console_set_percentage_length:
 * @self: A #FuConsole
 * @len: unsigned integer
 *
 * Sets the width of the progressbar percentage, which must be greater that 3.
 **/
void
fu_console_set_percentage_length(FuConsole *self, guint len)
{
	g_return_if_fail(FU_IS_CONSOLE(self));
	g_return_if_fail(len > 3);
	self->length_percentage = len;
}

static void
fu_console_init(FuConsole *self)
{
	self->length_percentage = 40;
	self->length_status = 25;
	self->spinner_count_up = TRUE;
	self->time_elapsed = g_timer_new();
	self->interactive = TRUE;
}

static void
fu_console_finalize(GObject *obj)
{
	FuConsole *self = FU_CONSOLE(obj);

	fu_console_reset_line(self);
	if (self->timer_source != 0)
		g_source_destroy(self->timer_source);
	if (self->main_ctx != NULL)
		g_main_context_unref(self->main_ctx);
	g_timer_destroy(self->time_elapsed);

	G_OBJECT_CLASS(fu_console_parent_class)->finalize(obj);
}

static void
fu_console_class_init(FuConsoleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_console_finalize;
}

/**
 * fu_console_new:
 *
 * Creates a new #FuConsole
 **/
FuConsole *
fu_console_new(void)
{
	FuConsole *self;
	self = g_object_new(FU_TYPE_CONSOLE, NULL);
	return FU_CONSOLE(self);
}
