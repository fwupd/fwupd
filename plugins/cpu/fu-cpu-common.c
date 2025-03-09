/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cpu-common.h"

const gchar *
fu_cpu_amd_stream_name_to_entry_sign_fixed_agesa_version(const gchar *stream_name)
{
	struct {
		const gchar *stream_name;
		const gchar *version;
	} version_map[] = {
	    {"NaplesPI", "1.0.0.P"},
	    {"RomePI", "1.0.0.L"},
	    {"MilanPI", "1.0.0.F"},
	    {"Genoa", "1.0.0.E"},
	    {"ComboAM5PI", "1.2.0.3"}, /* maybe */
	    {"MI300PI_SR5", "1.0.0.8"},
	    {"ComboAM4v2PI", "1.2.0.E"},
	    {"ComboAM4PI", "1.0.0.D"},
	    {"ComboAM5PI", "1.2.0.3"}, /* maybe */
	    {"ComboAM4v2PI", "1.2.0.E"},
	    {"ComboAM5PI", "1.2.0.3"}, /* maybe */
	    {"CastlePeakPI-SP3r3", "1.0.0.E"},
	    {"StormPeakPI-SP6", "1.0.0.1k"},
	    {"ChagallWSPI-sWRX8", "1.0.0.B"},
	    {"CastlePeakWSPI-sWRX8", "1.0.0.g"},
	    {"ChagallWSPI-sWRX8", "1.0.0.B"},
	    {"PicassoPI-FP5", "1.0.1.2b"},
	    {"RenoirPI-FP6", "1.0.0.Eb"},
	    {"CezannePI-FP6", "1.0.1.1b"},
	    {"MendocinoPI-FT6", "1.0.0.7b"},
	    {"RembrandtPI-FP7", "1.0.0.Bb"},
	    {"CezannePI-FP6", "1.0.1.1b"},
	    {"PhoenixPI-FP8-FP7", "1.2.0.0"},
	    {"DragonRangeFL1", "1.0.0.3g"},
	    {"SnowyOwl PI", "1.1.0.E"},
	    {"EmbRomePI-SP3", "1.0.0.D"},
	    {"EmbMilan PI-SP3", "1.0.0.A"},
	    {"EmbGenoaPI-SP5", "1.0.0.9"},
	    {"EmbeddedPI-FP5", "1.2.0.F"},
	    {"EmbeddedR2KPI", "1.0.0.5"},
	};
	for (guint i = 0; i < G_N_ELEMENTS(version_map); i++) {
		if (g_strcmp0(version_map[i].stream_name, stream_name) == 0)
			return version_map[i].version;
	}
	return NULL;
}

guint32
fu_cpu_amd_model_id_to_entry_sign_fixed_ucode_version(guint32 model_id)
{
	struct {
		guint32 model_id;
		guint32 ucode_version;
	} version_map[] = {
	    {0x00800F12, 0x08001278},
	    {0x00830F10, 0x0830107D},
	    {0x00A00F11, 0x0A0011DB},
	    {0x00A00F12, 0x0A001244},
	    {0x00A10F11, 0x0A101154},
	    {0x00A10F12, 0x0A10124F},
	    {0x00AA0F02, 0x0AA00219},
	};
	for (guint i = 0; i < G_N_ELEMENTS(version_map); i++) {
		if (version_map[i].model_id == model_id)
			return version_map[i].ucode_version;
	}
	return 0;
}
