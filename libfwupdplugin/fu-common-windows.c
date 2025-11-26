/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <gio/gio.h>
#include <shlwapi.h>
#include <sysinfoapi.h>
#include <timezoneapi.h>

#include "fu-common-private.h"

GPtrArray *
fu_common_get_block_devices(GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "getting block devices is not supported on Windows");
	return NULL;
}

guint64
fu_common_get_memory_size_impl(void)
{
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatusEx(&status);
	return (guint64)status.ullTotalPhys;
}

gchar *
fu_common_get_kernel_cmdline_impl(GError **error)
{
	return g_strdup("");
}

static gchar *
fu_common_convert_tzinfo_to_olson_id(const gchar *tzinfo, GError **error)
{
	struct {
		const gchar *tzinfo;
		const gchar *olson_id;
	} map[] = {{"Afghanistan", "Asia/Kabul"},
		   {"Alaskan", "America/Anchorage"},
		   {"Aleutian", "America/Adak"},
		   {"Altai", "Asia/Barnaul"},
		   {"Arab", "Asia/Riyadh"},
		   {"Arabian", "Asia/Dubai"},
		   {"Arabic", "Asia/Baghdad"},
		   {"Argentina", "America/Buenos_Aires"},
		   {"Astrakhan", "Europe/Astrakhan"},
		   {"Atlantic", "America/Halifax"},
		   {"AUS Central", "Australia/Darwin"},
		   {"Aus Central W.", "Australia/Eucla"},
		   {"AUS Eastern", "Australia/Sydney"},
		   {"Azerbaijan", "Asia/Baku"},
		   {"Azores", "Atlantic/Azores"},
		   {"Bahia", "America/Bahia"},
		   {"Bangladesh", "Asia/Dhaka"},
		   {"Belarus", "Europe/Minsk"},
		   {"Bougainville", "Pacific/Bougainville"},
		   {"Canada Central", "America/Regina"},
		   {"Cape Verde", "Atlantic/Cape_Verde"},
		   {"Caucasus", "Asia/Yerevan"},
		   {"Cen. Australia", "Australia/Adelaide"},
		   {"Central America", "America/Guatemala"},
		   {"Central", "America/Chicago"},
		   {"Central Asia", "Asia/Almaty"},
		   {"Central Brazilian", "America/Cuiaba"},
		   {"Central European", "Europe/Warsaw"},
		   {"Central Europe", "Europe/Budapest"},
		   {"Central Pacific", "Pacific/Guadalcanal"},
		   {"Central Standard Time (Mexico)", "America/Mexico_City"},
		   {"Chatham Islands", "Pacific/Chatham"},
		   {"China", "Asia/Shanghai"},
		   {"Cuba", "America/Havana"},
		   {"Dateline", "Etc/GMT+12"},
		   {"E. Africa", "Africa/Nairobi"},
		   {"Easter Island", "Pacific/Easter"},
		   {"Eastern", "America/New_York"},
		   {"Eastern Standard Time (Mexico)", "America/Cancun"},
		   {"E. Australia", "Australia/Brisbane"},
		   {"E. Europe", "Europe/Chisinau"},
		   {"Egypt", "Africa/Cairo"},
		   {"Ekaterinburg", "Asia/Yekaterinburg"},
		   {"E. South America", "America/Sao_Paulo"},
		   {"Fiji", "Pacific/Fiji"},
		   {"FILE", "Europe/Kiev"},
		   {"Georgian", "Asia/Tbilisi"},
		   {"GMT", "Europe/London"},
		   {"Greenland", "America/Godthab"},
		   {"Greenwich", "Atlantic/Reykjavik"},
		   {"GTB", "Europe/Bucharest"},
		   {"Haiti", "America/Port-au-Prince"},
		   {"Hawaiian", "Pacific/Honolulu"},
		   {"India", "Asia/Calcutta"},
		   {"Iran", "Asia/Tehran"},
		   {"Israel", "Asia/Jerusalem"},
		   {"Jordan", "Asia/Amman"},
		   {"Kaliningrad", "Europe/Kaliningrad"},
		   {"Korea", "Asia/Seoul"},
		   {"Libya", "Africa/Tripoli"},
		   {"Line Islands", "Pacific/Kiritimati"},
		   {"Lord Howe", "Australia/Lord_Howe"},
		   {"Magadan", "Asia/Magadan"},
		   {"Magallanes", "America/Punta_Arenas"},
		   {"Marquesas", "Pacific/Marquesas"},
		   {"Mauritius", "Indian/Mauritius"},
		   {"Middle East", "Asia/Beirut"},
		   {"Montevideo", "America/Montevideo"},
		   {"Morocco", "Africa/Casablanca"},
		   {"Mountain", "America/Denver"},
		   {"Mountain Standard Time (Mexico)", "America/Mazatlan"},
		   {"Myanmar", "Asia/Rangoon"},
		   {"Namibia", "Africa/Windhoek"},
		   {"N. Central Asia", "Asia/Novosibirsk"},
		   {"Nepal", "Asia/Katmandu"},
		   {"Newfoundland", "America/St_Johns"},
		   {"New Zealand", "Pacific/Auckland"},
		   {"Norfolk", "Pacific/Norfolk"},
		   {"North Asia", "Asia/Krasnoyarsk"},
		   {"North Asia East", "Asia/Irkutsk"},
		   {"North Korea", "Asia/Pyongyang"},
		   {"Omsk", "Asia/Omsk"},
		   {"Pacific", "America/Los_Angeles"},
		   {"Pacific SA", "America/Santiago"},
		   {"Pacific Standard Time (Mexico)", "America/Tijuana"},
		   {"Pakistan", "Asia/Karachi"},
		   {"Paraguay", "America/Asuncion"},
		   {"Qyzylorda", "Asia/Qyzylorda"},
		   {"Romance", "Europe/Paris"},
		   {"Russian", "Europe/Moscow"},
		   {"Russia Time Zone 10", "Asia/Srednekolymsk"},
		   {"Russia Time Zone 11", "Asia/Kamchatka"},
		   {"Russia Time Zone 3", "Europe/Samara"},
		   {"SA Eastern", "America/Cayenne"},
		   {"Saint Pierre", "America/Miquelon"},
		   {"Sakhalin", "Asia/Sakhalin"},
		   {"Samoa", "Pacific/Apia"},
		   {"Sao Tome", "Africa/Sao_Tome"},
		   {"SA Pacific", "America/Bogota"},
		   {"Saratov", "Europe/Saratov"},
		   {"SA Western", "America/La_Paz"},
		   {"SE Asia", "Asia/Bangkok"},
		   {"Singapore", "Asia/Singapore"},
		   {"South Africa", "Africa/Johannesburg"},
		   {"South Sudan", "Africa/Juba"},
		   {"Sri Lanka", "Asia/Colombo"},
		   {"Sudan", "Africa/Khartoum"},
		   {"Syria", "Asia/Damascus"},
		   {"Taipei", "Asia/Taipei"},
		   {"Tasmania", "Australia/Hobart"},
		   {"Tocantins", "America/Araguaina"},
		   {"Tokyo", "Asia/Tokyo"},
		   {"Tomsk", "Asia/Tomsk"},
		   {"Tonga", "Pacific/Tongatapu"},
		   {"Transbaikal", "Asia/Chita"},
		   {"Turkey", "Europe/Istanbul"},
		   {"Turks And Caicos", "America/Grand_Turk"},
		   {"Ulaanbaatar", "Asia/Ulaanbaatar"},
		   {"US Eastern", "America/Indianapolis"},
		   {"US Mountain", "America/Phoenix"},
		   {"UTC-02", "Etc/GMT+2"},
		   {"UTC-08", "Etc/GMT+8"},
		   {"UTC-09", "Etc/GMT+9"},
		   {"UTC-11", "Etc/GMT+11"},
		   {"UTC+12", "Etc/GMT-12"},
		   {"UTC+13", "Etc/GMT-13"},
		   {"UTC", "Etc/UTC"},
		   {"Venezuela", "America/Caracas"},
		   {"Vladivostok", "Asia/Vladivostok"},
		   {"Volgograd", "Europe/Volgograd"},
		   {"W. Australia", "Australia/Perth"},
		   {"W. Central Africa", "Africa/Lagos"},
		   {"West Asia", "Asia/Tashkent"},
		   {"West Bank", "Asia/Hebron"},
		   {"West Pacific", "Pacific/Port_Moresby"},
		   {"W. Europe", "Europe/Berlin"},
		   {"W. Mongolia", "Asia/Hovd"},
		   {"Yakutsk", "Asia/Yakutsk"},
		   {"Yukon", "America/Whitehorse"},
		   {NULL, NULL}};
	for (guint i = 0; map[i].tzinfo != NULL; i++) {
		if (g_strcmp0(tzinfo, map[i].tzinfo) == 0)
			return g_strdup(map[i].olson_id);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot map tzinfo '%s' to Olson ID",
		    tzinfo);
	return NULL;
}

gchar *
fu_common_get_olson_timezone_id_impl(GError **error)
{
	DWORD rc;
	TIME_ZONE_INFORMATION tzinfo = {0};
	gchar *suffix;
	g_autofree gchar *name = NULL;

	rc = GetTimeZoneInformation(&tzinfo);
	if (rc == TIME_ZONE_ID_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot get timezone information [%u]",
			    (guint)GetLastError());
		return NULL;
	}
	name = g_utf16_to_utf8(tzinfo.StandardName, -1, NULL, NULL, error);
	if (name == NULL) {
		g_prefix_error_literal(error, "cannot convert timezone name to UTF-8: ");
		return NULL;
	}

	/* make the lookup key shorter, then convert */
	suffix = g_strstr_len(name, -1, " Standard Time");
	if (suffix != NULL)
		*suffix = '\0';
	return fu_common_convert_tzinfo_to_olson_id(name, error);
}
