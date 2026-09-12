/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gi18n.h>

#include "fu-bios-settings-tui.h"

#ifdef HAVE_TUI
#include <curses.h>
#include <unistd.h>

#define FU_BIOS_SETTINGS_TUI_DELIMITER '|'

enum {
	FU_BIOS_SETTINGS_TUI_COLOR_HEADER = 1,
	FU_BIOS_SETTINGS_TUI_COLOR_GROUP,
	FU_BIOS_SETTINGS_TUI_COLOR_SELECTED,
	FU_BIOS_SETTINGS_TUI_COLOR_PENDING,
	FU_BIOS_SETTINGS_TUI_COLOR_DETAIL,
	FU_BIOS_SETTINGS_TUI_COLOR_BUTTON,
	FU_BIOS_SETTINGS_TUI_COLOR_DIALOG,
};

typedef struct {
	gchar *label;
	gchar *path;
	FwupdBiosSetting *setting;
} FuBiosSettingsTuiItem;

typedef struct {
	GPtrArray *settings;
	GPtrArray *items;
	GHashTable *pending;
	gchar *path;
	gchar *search;
	gchar *status;
	guint selected;
	guint top;
} FuBiosSettingsTui;

static chtype
fu_bios_settings_tui_color(gshort pair, chtype fallback)
{
	return has_colors() ? COLOR_PAIR(pair) : fallback;
}

static void
fu_bios_settings_tui_init_colors(void)
{
	gshort background = COLOR_BLACK;
	if (!has_colors())
		return;
	start_color();
	if (use_default_colors() != ERR)
		background = -1;
	init_pair(FU_BIOS_SETTINGS_TUI_COLOR_HEADER, COLOR_WHITE, COLOR_BLUE);
	init_pair(FU_BIOS_SETTINGS_TUI_COLOR_GROUP, COLOR_CYAN, background);
	init_pair(FU_BIOS_SETTINGS_TUI_COLOR_SELECTED, COLOR_BLACK, COLOR_CYAN);
	init_pair(FU_BIOS_SETTINGS_TUI_COLOR_PENDING, COLOR_YELLOW, background);
	init_pair(FU_BIOS_SETTINGS_TUI_COLOR_DETAIL, COLOR_GREEN, background);
	init_pair(FU_BIOS_SETTINGS_TUI_COLOR_BUTTON, COLOR_BLACK, COLOR_GREEN);
	init_pair(FU_BIOS_SETTINGS_TUI_COLOR_DIALOG, COLOR_CYAN, background);
}

static void
fu_bios_settings_tui_item_free(FuBiosSettingsTuiItem *item)
{
	g_free(item->label);
	g_free(item->path);
	g_free(item);
}

static gint
fu_bios_settings_tui_item_sort_cb(gconstpointer a, gconstpointer b)
{
	const FuBiosSettingsTuiItem *item_a = *((FuBiosSettingsTuiItem *const *)a);
	const FuBiosSettingsTuiItem *item_b = *((FuBiosSettingsTuiItem *const *)b);
	if ((item_a->setting == NULL) != (item_b->setting == NULL))
		return item_a->setting == NULL ? -1 : 1;
	return g_utf8_collate(item_a->label, item_b->label);
}

static const gchar *
fu_bios_settings_tui_setting_key(FwupdBiosSetting *setting)
{
	const gchar *id = fwupd_bios_setting_get_id(setting);
	return id != NULL ? id : fwupd_bios_setting_get_name(setting);
}

static gboolean
fu_bios_settings_tui_matches_search(const gchar *name, const gchar *search)
{
	g_autofree gchar *name_folded = g_utf8_casefold(name, -1);
	g_autofree gchar *search_folded = g_utf8_casefold(search, -1);
	return g_strstr_len(name_folded, -1, search_folded) != NULL;
}

static void
fu_bios_settings_tui_rebuild(FuBiosSettingsTui *self)
{
	g_autoptr(GHashTable) groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_autofree gchar *prefix =
	    self->path[0] != '\0'
		? g_strdup_printf("%s%c", self->path, FU_BIOS_SETTINGS_TUI_DELIMITER)
		: g_strdup("");

	g_ptr_array_set_size(self->items, 0);
	for (guint i = 0; i < self->settings->len; i++) {
		FwupdBiosSetting *setting = g_ptr_array_index(self->settings, i);
		const gchar *name = fwupd_bios_setting_get_name(setting);
		const gchar *remainder;
		const gchar *separator;
		FuBiosSettingsTuiItem *item;

		if (name == NULL)
			continue;
		if (self->search != NULL && self->search[0] != '\0') {
			if (!fu_bios_settings_tui_matches_search(name, self->search))
				continue;
			item = g_new0(FuBiosSettingsTuiItem, 1);
			item->label = g_strdup(name);
			item->setting = setting;
			g_ptr_array_add(self->items, item);
			continue;
		}
		if (!g_str_has_prefix(name, prefix))
			continue;
		remainder = name + strlen(prefix);
		if (remainder[0] == '\0')
			continue;
		separator = strchr(remainder, FU_BIOS_SETTINGS_TUI_DELIMITER);
		item = g_new0(FuBiosSettingsTuiItem, 1);
		if (separator != NULL) {
			item->label = g_strndup(remainder, separator - remainder);
			if (g_hash_table_contains(groups, item->label)) {
				fu_bios_settings_tui_item_free(item);
				continue;
			}
			g_hash_table_add(groups, g_strdup(item->label));
			item->path = prefix[0] != '\0'
					 ? g_strdup_printf("%s%s", prefix, item->label)
					 : g_strdup(item->label);
		} else {
			item->label = g_strdup(remainder);
			item->setting = setting;
		}
		g_ptr_array_add(self->items, item);
	}
	g_ptr_array_sort(self->items, fu_bios_settings_tui_item_sort_cb);
	if (self->items->len == 0) {
		self->selected = 0;
		self->top = 0;
		return;
	}
	if (self->selected >= self->items->len)
		self->selected = self->items->len - 1;
	if (self->top > self->selected)
		self->top = self->selected;
}

static void
fu_bios_settings_tui_enter_single_group(FuBiosSettingsTui *self)
{
	while (self->items->len == 1) {
		FuBiosSettingsTuiItem *item = g_ptr_array_index(self->items, 0);
		if (item->setting != NULL)
			return;
		g_free(self->path);
		self->path = g_strdup(item->path);
		self->selected = 0;
		self->top = 0;
		fu_bios_settings_tui_rebuild(self);
	}
}

static void
fu_bios_settings_tui_addnstr(gint y, gint x, gint width, const gchar *text)
{
	if (text == NULL || y < 0 || x < 0 || width <= 0)
		return;
	(void)mvaddnstr(y, x, text, width);
}

static void
fu_bios_settings_tui_draw_details(FuBiosSettingsTui *self,
				  FuBiosSettingsTuiItem *item,
				  gint x,
				  gint width,
				  gint height)
{
	FwupdBiosSetting *setting;
	const gchar *current;
	const gchar *description;
	const gchar *pending;
	gint row = 3;

	if (item == NULL || item->setting == NULL) {
		fu_bios_settings_tui_addnstr(row,
					     x,
					     width,
					     _("Open this group to view its settings."));
		return;
	}
	setting = item->setting;
	current = fwupd_bios_setting_get_current_value(setting);
	description = fwupd_bios_setting_get_description(setting);
	pending = g_hash_table_lookup(self->pending, fu_bios_settings_tui_setting_key(setting));
	attron(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_DETAIL, A_BOLD) | A_BOLD);
	fu_bios_settings_tui_addnstr(row++, x, width, item->label);
	attroff(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_DETAIL, A_BOLD) | A_BOLD);
	if (description != NULL && row < height - 3)
		fu_bios_settings_tui_addnstr(row++, x, width, description);
	row++;
	if (row < height - 3) {
		g_autofree gchar *value =
		    g_strdup_printf("%s: %s", _("Current value"), current != NULL ? current : "—");
		fu_bios_settings_tui_addnstr(row++, x, width, value);
	}
	if (pending != NULL && row < height - 3) {
		g_autofree gchar *value = g_strdup_printf("%s: %s", _("New value"), pending);
		attron(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_PENDING, A_BOLD) |
		       A_BOLD);
		fu_bios_settings_tui_addnstr(row++, x, width, value);
		attroff(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_PENDING, A_BOLD) |
			A_BOLD);
	}
	if (fwupd_bios_setting_get_read_only(setting) && row < height - 3)
		fu_bios_settings_tui_addnstr(row++, x, width, _("Read only"));
	if (fwupd_bios_setting_get_kind(setting) == FWUPD_BIOS_SETTING_KIND_INTEGER &&
	    row < height - 3) {
		g_autofree gchar *range = g_strdup_printf(
		    "%s: %" G_GUINT64_FORMAT "…%" G_GUINT64_FORMAT " (%" G_GUINT64_FORMAT ")",
		    _("Range"),
		    fwupd_bios_setting_get_lower_bound(setting),
		    fwupd_bios_setting_get_upper_bound(setting),
		    fwupd_bios_setting_get_scalar_increment(setting));
		fu_bios_settings_tui_addnstr(row++, x, width, range);
	}
}

static void
fu_bios_settings_tui_draw(FuBiosSettingsTui *self)
{
	FuBiosSettingsTuiItem *selected_item = NULL;
	gint height;
	gint width;
	gint list_width;
	gint list_rows;

	getmaxyx(stdscr, height, width);
	erase();
	if (height < 15 || width < 60) {
		fu_bios_settings_tui_addnstr(0, 0, width, _("Terminal must be at least 60×15"));
		refresh();
		return;
	}
	list_width = MAX(30, width / 2);
	list_rows = height - 6;
	if (self->selected < self->items->len)
		selected_item = g_ptr_array_index(self->items, self->selected);
	if (self->selected < self->top)
		self->top = self->selected;
	if (self->selected >= self->top + (guint)list_rows)
		self->top = self->selected - list_rows + 1;

	attron(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_HEADER, A_REVERSE) | A_BOLD);
	mvhline(0, 0, ' ', width);
	fu_bios_settings_tui_addnstr(0, 2, width - 4, _("fwupd BIOS Settings"));
	attroff(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_HEADER, A_REVERSE) | A_BOLD);
	if (self->search != NULL) {
		g_autofree gchar *title = g_strdup_printf("%s: %s", _("Search"), self->search);
		fu_bios_settings_tui_addnstr(1, 2, width - 4, title);
	} else {
		g_autofree gchar *title =
		    self->path[0] != '\0' ? g_strdup_printf("/ %s", self->path) : g_strdup("/");
		fu_bios_settings_tui_addnstr(1, 2, width - 4, title);
	}
	mvhline(2, 0, ACS_HLINE, width);
	mvvline(2, list_width, ACS_VLINE, height - 4);
	for (gint row = 0; row < list_rows; row++) {
		guint idx = self->top + row;
		FuBiosSettingsTuiItem *item;
		g_autofree gchar *line = NULL;
		const gchar *pending;
		if (idx >= self->items->len)
			break;
		item = g_ptr_array_index(self->items, idx);
		pending = item->setting != NULL
			      ? g_hash_table_lookup(self->pending,
						    fu_bios_settings_tui_setting_key(item->setting))
			      : NULL;
		line = item->setting == NULL
			   ? g_strdup_printf(" ▸ %s", item->label)
			   : g_strdup_printf(" %s%s", pending != NULL ? "* " : "  ", item->label);
		if (idx == self->selected)
			attron(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_SELECTED,
							  A_REVERSE));
		else if (pending != NULL)
			attron(
			    fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_PENDING, A_BOLD));
		else if (item->setting == NULL)
			attron(
			    fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_GROUP, A_BOLD));
		fu_bios_settings_tui_addnstr(3 + row, 0, list_width, line);
		if (idx == self->selected)
			attroff(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_SELECTED,
							   A_REVERSE));
		else if (pending != NULL)
			attroff(
			    fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_PENDING, A_BOLD));
		else if (item->setting == NULL)
			attroff(
			    fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_GROUP, A_BOLD));
	}
	fu_bios_settings_tui_draw_details(self,
					  selected_item,
					  list_width + 2,
					  width - list_width - 3,
					  height);
	mvhline(height - 3, 0, ACS_HLINE, width);
	if (self->status != NULL) {
		attron(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_PENDING, A_BOLD));
		fu_bios_settings_tui_addnstr(height - 2, 1, width - 2, self->status);
		attroff(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_PENDING, A_BOLD));
	} else {
		fu_bios_settings_tui_addnstr(
		    height - 2,
		    1,
		    width - 2,
		    _("Click/Enter: open/edit   ←: back   /: search   a: apply   q: cancel"));
	}
	attron(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_BUTTON, A_REVERSE) | A_BOLD);
	fu_bios_settings_tui_addnstr(height - 1, 1, 11, _("[ Apply ]"));
	fu_bios_settings_tui_addnstr(height - 1, 14, 12, _("[ Cancel ]"));
	fu_bios_settings_tui_addnstr(height - 1, 27, 10, _("[ Back ]"));
	attroff(fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_BUTTON, A_REVERSE) | A_BOLD);
	refresh();
}

static void
fu_bios_settings_tui_draw_dialog_buttons(WINDOW *window, gint height, gint width)
{
	gint ok_x = width / 2 - 13;
	gint cancel_x = width / 2 + 2;
	chtype attr =
	    fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_BUTTON, A_REVERSE) | A_BOLD;
	wattron(window, attr);
	mvwaddnstr(window, height - 2, ok_x, _("[ OK ]"), 10);
	mvwaddnstr(window, height - 2, cancel_x, _("[ Cancel ]"), 12);
	wattroff(window, attr);
}

static gint
fu_bios_settings_tui_dialog_button(const MEVENT *event, gint x, gint y, gint height, gint width)
{
	gint ok_x = x + width / 2 - 13;
	gint cancel_x = x + width / 2 + 2;
	if ((event->bstate & BUTTON1_CLICKED) == 0 || event->y != y + height - 2)
		return 0;
	if (event->x >= ok_x && event->x < ok_x + 10)
		return 1;
	if (event->x >= cancel_x && event->x < cancel_x + 12)
		return -1;
	return 0;
}

static gchar *
fu_bios_settings_tui_edit_enumeration(FwupdBiosSetting *setting)
{
	GPtrArray *values = fwupd_bios_setting_get_possible_values(setting);
	const gchar *current = fwupd_bios_setting_get_current_value(setting);
	guint selected = 0;
	guint top = 0;
	WINDOW *window;
	gint screen_height;
	gint screen_width;
	gint list_rows;
	gint height;
	gint width;
	gint y;
	gint x;

	if (values == NULL || values->len == 0)
		return NULL;
	for (guint i = 0; i < values->len; i++) {
		if (g_strcmp0(g_ptr_array_index(values, i), current) == 0) {
			selected = i;
			break;
		}
	}
	getmaxyx(stdscr, screen_height, screen_width);
	height = MIN((gint)values->len + 5, screen_height - 4);
	width = MIN(70, screen_width - 4);
	list_rows = height - 4;
	y = (screen_height - height) / 2;
	x = (screen_width - width) / 2;
	window = newwin(height, width, y, x);
	if (window == NULL)
		return NULL;
	keypad(window, TRUE);
	for (;;) {
		MEVENT event;
		gint button;
		gint key;
		werase(window);
		box(window, 0, 0);
		wattron(window,
			fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_DIALOG, A_BOLD) |
			    A_BOLD);
		mvwaddnstr(window, 0, 2, _(" Select a value "), width - 4);
		wattroff(window,
			 fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_DIALOG, A_BOLD) |
			     A_BOLD);
		for (gint row = 0; row < list_rows; row++) {
			guint idx = top + row;
			if (idx >= values->len)
				break;
			if (idx == selected)
				wattron(
				    window,
				    fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_SELECTED,
							       A_REVERSE));
			mvwaddnstr(window, row + 2, 2, g_ptr_array_index(values, idx), width - 4);
			if (idx == selected)
				wattroff(
				    window,
				    fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_SELECTED,
							       A_REVERSE));
		}
		fu_bios_settings_tui_draw_dialog_buttons(window, height, width);
		wrefresh(window);
		key = wgetch(window);
		if (key == KEY_UP && selected > 0)
			selected--;
		else if (key == KEY_RESIZE) {
			delwin(window);
			return NULL;
		} else if (key == KEY_DOWN && selected + 1 < values->len)
			selected++;
		else if (key == KEY_NPAGE)
			selected = MIN(values->len - 1, selected + list_rows);
		else if (key == KEY_PPAGE)
			selected = selected > (guint)list_rows ? selected - list_rows : 0;
		else if (key == '\n' || key == KEY_ENTER) {
			gchar *value = g_strdup(g_ptr_array_index(values, selected));
			delwin(window);
			return value;
		} else if (key == 27 || key == 'q') {
			delwin(window);
			return NULL;
		} else if (key == KEY_MOUSE && getmouse(&event) == OK) {
			button = fu_bios_settings_tui_dialog_button(&event, x, y, height, width);
			if (button > 0) {
				gchar *value = g_strdup(g_ptr_array_index(values, selected));
				delwin(window);
				return value;
			}
			if (button < 0) {
				delwin(window);
				return NULL;
			}
			if ((event.bstate & BUTTON4_PRESSED) != 0 && selected > 0)
				selected--;
			else if ((event.bstate & BUTTON5_PRESSED) != 0 &&
				 selected + 1 < values->len)
				selected++;
			else if ((event.bstate & BUTTON1_CLICKED) != 0 && event.x >= x + 1 &&
				 event.x < x + width - 1 && event.y >= y + 2 &&
				 event.y < y + 2 + list_rows) {
				guint idx = top + event.y - y - 2;
				if (idx < values->len)
					selected = idx;
			}
		}
		if (selected < top)
			top = selected;
		if (selected >= top + (guint)list_rows)
			top = selected - list_rows + 1;
	}
}

static guint64
fu_bios_settings_tui_slider_move(guint64 value,
				 guint64 lower,
				 guint64 upper,
				 guint64 step,
				 gint direction,
				 guint count)
{
	for (guint i = 0; i < count; i++) {
		if (direction < 0)
			value = value - lower < step ? lower : value - step;
		else
			value = upper - value < step ? upper : value + step;
	}
	return value;
}

static gchar *
fu_bios_settings_tui_edit_integer(FwupdBiosSetting *setting)
{
	const gchar *current = fwupd_bios_setting_get_current_value(setting);
	guint64 lower = fwupd_bios_setting_get_lower_bound(setting);
	guint64 upper = fwupd_bios_setting_get_upper_bound(setting);
	guint64 step = MAX(fwupd_bios_setting_get_scalar_increment(setting), 1);
	guint64 value = lower;
	guint64 slider_upper;
	guint64 steps;
	WINDOW *window;
	gint screen_height;
	gint screen_width;
	gint height = 10;
	gint width;
	gint track_width;
	gint y;
	gint x;

	if (upper < lower)
		return NULL;
	steps = (upper - lower) / step;
	slider_upper = lower + steps * step;
	if (current != NULL)
		(void)g_ascii_string_to_unsigned(current, 10, lower, upper, &value, NULL);
	value = lower + ((value - lower) / step) * step;
	getmaxyx(stdscr, screen_height, screen_width);
	width = MIN(76, screen_width - 4);
	track_width = width - 6;
	y = (screen_height - height) / 2;
	x = (screen_width - width) / 2;
	window = newwin(height, width, y, x);
	if (window == NULL)
		return NULL;
	keypad(window, TRUE);
	for (;;) {
		MEVENT event;
		g_autofree gchar *maximum = g_strdup_printf("%" G_GUINT64_FORMAT, slider_upper);
		g_autofree gchar *minimum = g_strdup_printf("%" G_GUINT64_FORMAT, lower);
		g_autofree gchar *shown = g_strdup_printf("%" G_GUINT64_FORMAT, value);
		gint button;
		gint key;
		gint position = 0;
		werase(window);
		box(window, 0, 0);
		wattron(window,
			fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_DIALOG, A_BOLD) |
			    A_BOLD);
		mvwaddnstr(window, 0, 2, _(" Select a value "), width - 4);
		wattroff(window,
			 fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_DIALOG, A_BOLD) |
			     A_BOLD);
		mvwaddnstr(window, 2, 2, fwupd_bios_setting_get_name(setting), width - 4);
		mvwaddnstr(window, 3, (width - (gint)strlen(shown)) / 2, shown, width - 4);
		mvwhline(window, 5, 3, ACS_HLINE, track_width);
		if (slider_upper > lower)
			position = (gint)(((long double)(value - lower) * (track_width - 1)) /
					  (long double)(slider_upper - lower));
		mvwaddch(
		    window,
		    5,
		    3 + position,
		    ACS_DIAMOND |
			fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_PENDING, A_BOLD) |
			A_BOLD);
		mvwaddnstr(window, 6, 2, minimum, width / 2 - 2);
		mvwaddnstr(window,
			   6,
			   width - (gint)strlen(maximum) - 2,
			   maximum,
			   (gint)strlen(maximum));
		fu_bios_settings_tui_draw_dialog_buttons(window, height, width);
		wrefresh(window);
		key = wgetch(window);
		if (key == KEY_LEFT || key == KEY_DOWN)
			value = fu_bios_settings_tui_slider_move(value,
								 lower,
								 slider_upper,
								 step,
								 -1,
								 1);
		else if (key == KEY_RIGHT || key == KEY_UP)
			value = fu_bios_settings_tui_slider_move(value,
								 lower,
								 slider_upper,
								 step,
								 1,
								 1);
		else if (key == KEY_RESIZE) {
			delwin(window);
			return NULL;
		} else if (key == KEY_PPAGE)
			value = fu_bios_settings_tui_slider_move(value,
								 lower,
								 slider_upper,
								 step,
								 1,
								 10);
		else if (key == KEY_NPAGE)
			value = fu_bios_settings_tui_slider_move(value,
								 lower,
								 slider_upper,
								 step,
								 -1,
								 10);
		else if (key == KEY_HOME)
			value = lower;
		else if (key == KEY_END)
			value = slider_upper;
		else if (key == '\n' || key == KEY_ENTER) {
			gchar *result = g_strdup(shown);
			delwin(window);
			return result;
		} else if (key == 27 || key == 'q') {
			delwin(window);
			return NULL;
		} else if (key == KEY_MOUSE && getmouse(&event) == OK) {
			button = fu_bios_settings_tui_dialog_button(&event, x, y, height, width);
			if (button > 0) {
				gchar *result = g_strdup(shown);
				delwin(window);
				return result;
			}
			if (button < 0) {
				delwin(window);
				return NULL;
			}
			if ((event.bstate & BUTTON4_PRESSED) != 0)
				value = fu_bios_settings_tui_slider_move(value,
									 lower,
									 slider_upper,
									 step,
									 1,
									 1);
			else if ((event.bstate & BUTTON5_PRESSED) != 0)
				value = fu_bios_settings_tui_slider_move(value,
									 lower,
									 slider_upper,
									 step,
									 -1,
									 1);
			else if ((event.bstate & BUTTON1_CLICKED) != 0 && event.y == y + 5 &&
				 event.x >= x + 3 && event.x < x + 3 + track_width) {
				guint64 divisor = track_width - 1;
				guint64 offset = event.x - x - 3;
				guint64 selected_step =
				    (steps / divisor) * offset +
				    (((steps % divisor) * offset + divisor / 2) / divisor);
				value = lower + selected_step * step;
			}
		}
	}
}

static gchar *
fu_bios_settings_tui_edit_text(FwupdBiosSetting *setting)
{
	g_autoptr(GString) input = g_string_new(NULL);
	guint64 lower = fwupd_bios_setting_get_lower_bound(setting);
	guint64 upper = fwupd_bios_setting_get_upper_bound(setting);
	WINDOW *window;
	gint screen_height;
	gint screen_width;
	gint height = 9;
	gint width;
	gint y;
	gint x;

	getmaxyx(stdscr, screen_height, screen_width);
	width = MIN(76, screen_width - 4);
	y = (screen_height - height) / 2;
	x = (screen_width - width) / 2;
	window = newwin(height, width, y, x);
	if (window == NULL)
		return NULL;
	keypad(window, TRUE);
	curs_set(1);
	for (;;) {
		MEVENT event;
		wint_t key;
		gint button;
		gint rc;
		werase(window);
		box(window, 0, 0);
		wattron(window,
			fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_DIALOG, A_BOLD) |
			    A_BOLD);
		mvwaddnstr(window, 0, 2, _(" Enter a value "), width - 4);
		wattroff(window,
			 fu_bios_settings_tui_color(FU_BIOS_SETTINGS_TUI_COLOR_DIALOG, A_BOLD) |
			     A_BOLD);
		mvwaddnstr(window, 2, 2, fwupd_bios_setting_get_name(setting), width - 4);
		mvwaddnstr(window, 4, 2, "> ", 2);
		mvwaddnstr(window, 4, 4, input->str, width - 7);
		fu_bios_settings_tui_draw_dialog_buttons(window, height, width);
		wmove(window, 4, MIN(width - 4, 4 + (gint)g_utf8_strlen(input->str, input->len)));
		wrefresh(window);
		rc = wget_wch(window, &key);
		if (rc == KEY_CODE_YES && key == KEY_RESIZE) {
			curs_set(0);
			delwin(window);
			return NULL;
		}
		if (rc == KEY_CODE_YES && key == KEY_MOUSE && getmouse(&event) == OK) {
			button = fu_bios_settings_tui_dialog_button(&event, x, y, height, width);
			if (button > 0 && input->len >= lower && input->len <= upper) {
				gchar *result = g_string_free(g_steal_pointer(&input), FALSE);
				curs_set(0);
				delwin(window);
				return result;
			}
			if (button < 0) {
				curs_set(0);
				delwin(window);
				return NULL;
			}
			continue;
		}
		if ((rc == OK && (key == '\n' || key == '\r')) ||
		    (rc == KEY_CODE_YES && key == KEY_ENTER)) {
			gchar *result;
			if (input->len < lower || input->len > upper)
				continue;
			result = g_string_free(g_steal_pointer(&input), FALSE);
			curs_set(0);
			delwin(window);
			return result;
		}
		if ((rc == OK && (key == 27 || key == 3)) ||
		    (rc == KEY_CODE_YES && key == KEY_EXIT)) {
			curs_set(0);
			delwin(window);
			return NULL;
		}
		if ((rc == OK && (key == '\b' || key == 127)) ||
		    (rc == KEY_CODE_YES && key == KEY_BACKSPACE)) {
			gchar *previous =
			    g_utf8_find_prev_char(input->str, input->str + input->len);
			if (previous != NULL)
				g_string_truncate(input, previous - input->str);
			continue;
		}
		if (rc == OK && key <= G_MAXUINT32 && g_unichar_isprint((gunichar)key)) {
			gchar utf8[6];
			gint utf8sz = g_unichar_to_utf8((gunichar)key, utf8);
			if (input->len <= upper && (guint64)utf8sz <= upper - input->len)
				g_string_append_len(input, utf8, utf8sz);
		}
	}
}

static void
fu_bios_settings_tui_edit(FuBiosSettingsTui *self, FuBiosSettingsTuiItem *item)
{
	FwupdBiosSetting *setting;
	g_autofree gchar *value = NULL;
	const gchar *key;

	if (item == NULL || item->setting == NULL)
		return;
	setting = item->setting;
	g_clear_pointer(&self->status, g_free);
	if (fwupd_bios_setting_get_read_only(setting)) {
		self->status = g_strdup(_("This setting is read only"));
		return;
	}
	if (fwupd_bios_setting_get_kind(setting) == FWUPD_BIOS_SETTING_KIND_ENUMERATION)
		value = fu_bios_settings_tui_edit_enumeration(setting);
	else if (fwupd_bios_setting_get_kind(setting) == FWUPD_BIOS_SETTING_KIND_INTEGER)
		value = fu_bios_settings_tui_edit_integer(setting);
	else
		value = fu_bios_settings_tui_edit_text(setting);
	if (value == NULL)
		return;
	key = fu_bios_settings_tui_setting_key(setting);
	if (g_strcmp0(value, fwupd_bios_setting_get_current_value(setting)) == 0)
		g_hash_table_remove(self->pending, key);
	else
		g_hash_table_replace(self->pending, g_strdup(key), g_steal_pointer(&value));
}

static void
fu_bios_settings_tui_open(FuBiosSettingsTui *self)
{
	FuBiosSettingsTuiItem *item;
	if (self->selected >= self->items->len)
		return;
	item = g_ptr_array_index(self->items, self->selected);
	if (item->setting != NULL) {
		fu_bios_settings_tui_edit(self, item);
		return;
	}
	g_free(self->path);
	self->path = g_strdup(item->path);
	self->selected = 0;
	self->top = 0;
	fu_bios_settings_tui_rebuild(self);
	fu_bios_settings_tui_enter_single_group(self);
}

static void
fu_bios_settings_tui_back(FuBiosSettingsTui *self)
{
	FuBiosSettingsTuiItem *item;
	gchar *separator;
	if (self->search != NULL) {
		g_clear_pointer(&self->search, g_free);
		self->selected = 0;
		self->top = 0;
		fu_bios_settings_tui_rebuild(self);
		return;
	}
	while (self->path[0] != '\0') {
		separator = strrchr(self->path, FU_BIOS_SETTINGS_TUI_DELIMITER);
		if (separator != NULL)
			*separator = '\0';
		else
			self->path[0] = '\0';
		self->selected = 0;
		self->top = 0;
		fu_bios_settings_tui_rebuild(self);
		if (self->items->len != 1)
			return;
		item = g_ptr_array_index(self->items, 0);
		if (item->setting != NULL)
			return;
	}
}

static void
fu_bios_settings_tui_search(FuBiosSettingsTui *self)
{
	gchar input[128] = {0};
	gint height;
	gint width;
	getmaxyx(stdscr, height, width);
	move(height - 2, 0);
	clrtoeol();
	mvaddnstr(height - 2, 1, _("Search: "), width - 2);
	echo();
	curs_set(1);
	(void)getnstr(input, MIN((gint)sizeof(input) - 1, width - 10));
	noecho();
	curs_set(0);
	g_free(self->search);
	self->search = input[0] != '\0' ? g_strdup(input) : NULL;
	self->selected = 0;
	self->top = 0;
	fu_bios_settings_tui_rebuild(self);
}

GHashTable *
fu_bios_settings_tui_run(GPtrArray *settings, GError **error)
{
	FuBiosSettingsTui self = {
	    .settings = settings,
	    .items = g_ptr_array_new_with_free_func((GDestroyNotify)fu_bios_settings_tui_item_free),
	    .pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free),
	    .path = g_strdup(""),
	};
	gboolean apply = FALSE;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "the BIOS settings interface requires a terminal");
		g_ptr_array_unref(self.items);
		g_hash_table_unref(self.pending);
		g_free(self.path);
		return NULL;
	}
	if (initscr() == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to initialize the terminal interface");
		g_ptr_array_unref(self.items);
		g_hash_table_unref(self.pending);
		g_free(self.path);
		return NULL;
	}
	raw();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	fu_bios_settings_tui_init_colors();
	(void)mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
	(void)mouseinterval(200);
	fu_bios_settings_tui_rebuild(&self);
	fu_bios_settings_tui_enter_single_group(&self);
	for (;;) {
		MEVENT event;
		gint height;
		gint width;
		gint key;
		fu_bios_settings_tui_draw(&self);
		key = getch();
		getmaxyx(stdscr, height, width);
		g_clear_pointer(&self.status, g_free);
		if (key == KEY_RESIZE)
			continue;
		if (height < 15 || width < 60) {
			if (key == 'q' || key == 27 || key == 3)
				break;
			continue;
		}
		if ((key == KEY_UP || key == 'k') && self.selected > 0)
			self.selected--;
		else if ((key == KEY_DOWN || key == 'j') && self.selected + 1 < self.items->len)
			self.selected++;
		else if (key == KEY_HOME)
			self.selected = 0;
		else if (key == KEY_END && self.items->len > 0)
			self.selected = self.items->len - 1;
		else if (key == KEY_NPAGE && self.items->len > 0)
			self.selected = MIN(self.items->len - 1, self.selected + height - 6);
		else if (key == KEY_PPAGE)
			self.selected =
			    self.selected > (guint)(height - 6) ? self.selected - height + 6 : 0;
		else if (key == '\n' || key == KEY_ENTER || key == KEY_RIGHT || key == ' ')
			fu_bios_settings_tui_open(&self);
		else if (key == KEY_LEFT || key == KEY_BACKSPACE || key == 127)
			fu_bios_settings_tui_back(&self);
		else if (key == '/')
			fu_bios_settings_tui_search(&self);
		else if (key == 'a' || key == KEY_F(10)) {
			apply = TRUE;
			break;
		} else if (key == 'q' || key == 27 || key == 3) {
			break;
		} else if (key == KEY_MOUSE && getmouse(&event) == OK) {
			if ((event.bstate & BUTTON4_PRESSED) != 0 && self.selected > 0)
				self.selected--;
			else if ((event.bstate & BUTTON5_PRESSED) != 0 &&
				 self.selected + 1 < self.items->len)
				self.selected++;
			else if ((event.bstate & (BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED)) != 0 &&
				 event.y >= 3 && event.y < height - 3 &&
				 event.x < MAX(30, width / 2)) {
				guint idx = self.top + event.y - 3;
				if (idx < self.items->len) {
					self.selected = idx;
					fu_bios_settings_tui_open(&self);
				}
			} else if ((event.bstate & BUTTON1_CLICKED) != 0 && event.y == height - 1) {
				if (event.x >= 1 && event.x < 12) {
					apply = TRUE;
					break;
				}
				if (event.x >= 14 && event.x < 26)
					break;
				if (event.x >= 27 && event.x < 37)
					fu_bios_settings_tui_back(&self);
			}
		}
	}
	endwin();
	if (!apply)
		g_hash_table_remove_all(self.pending);
	g_ptr_array_unref(self.items);
	g_free(self.path);
	g_free(self.search);
	g_free(self.status);
	return self.pending;
}

#else

GHashTable *
fu_bios_settings_tui_run(GPtrArray *settings, GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "fwupd was built without terminal user interface support");
	return NULL;
}

#endif
