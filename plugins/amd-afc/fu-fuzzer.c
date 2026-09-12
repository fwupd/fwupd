/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-common.h"

#define HII_PACKAGE_FORMS   0x02U
#define HII_PACKAGE_STRINGS 0x04U
#define HII_STRINGS_HDRSZ   50U

static FuContext *fuzzer_ctx;

int
LLVMFuzzerInitialize(int *argc, char ***argv);
int
LLVMFuzzerTestOneInput(const guint8 *data, gsize size);

static GByteArray *
fu_fuzzer_build_package(const guint8 *data, gsize size, guint8 kind, gsize header_size)
{
	g_autoptr(GByteArray) package = g_byte_array_sized_new(header_size + size + 1);
	guint32 package_header;

	g_byte_array_set_size(package, header_size);
	memset(package->data, 0, package->len);
	if (kind == HII_PACKAGE_STRINGS) {
		fu_memwrite_uint32(package->data + 4, header_size, G_LITTLE_ENDIAN);
		package->data[46] = 'e';
		package->data[47] = 'n';
	}
	g_byte_array_append(package, data, size);
	if (kind == HII_PACKAGE_STRINGS) {
		const guint8 end = 0;
		g_byte_array_append(package, &end, sizeof(end));
	}
	package_header = ((guint32)kind << 24) | package->len;
	fu_memwrite_uint32(package->data, package_header, G_LITTLE_ENDIAN);
	return g_steal_pointer(&package);
}

static void
fu_fuzzer_seed_strings(FuAmdAfcState *state)
{
	for (guint i = 0; i < 32; i++)
		g_ptr_array_add(state->strings, g_strdup_printf("string-%u", i));
}

static void
fu_fuzzer_forms(FuAmdAfcState *state, const guint8 *data, gsize size)
{
	g_autoptr(GByteArray) package = fu_fuzzer_build_package(data, size, HII_PACKAGE_FORMS, 4);
	g_autoptr(GPtrArray) path = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GError) error = NULL;

	fu_fuzzer_seed_strings(state);
	if (!fu_amd_afc_hii_parse_forms(state, package->data, package->len, &error))
		return;
	if (state->forms->len == 0 || state->formset_name == NULL)
		return;
	g_ptr_array_add(path, g_strdup(state->formset_name));
	for (guint i = 0; i < state->forms->len; i++) {
		if (!fu_amd_afc_hii_assign_form_paths(state, i, path, NULL))
			return;
	}
}

static void
fu_fuzzer_strings(FuAmdAfcState *state, const guint8 *data, gsize size)
{
	g_autoptr(GByteArray) package =
	    fu_fuzzer_build_package(data, size, HII_PACKAGE_STRINGS, HII_STRINGS_HDRSZ);

	(void)fu_amd_afc_hii_strings_parse_package(state, package->data, package->len, NULL);
}

static void
fu_fuzzer_table(FuAmdAfcState *state, const guint8 *data, gsize size)
{
	g_autoptr(GBytes) bytes = g_bytes_new_static(data, size);

	(void)fu_amd_afc_state_parse_table(state, bytes, NULL);
}

static void
fu_fuzzer_config(const guint8 *data, gsize size)
{
	g_autoptr(GBytes) bytes = g_bytes_new_static(data, size);

	(void)fu_amd_afc_config_validate(bytes, NULL);
}

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)g_setenv("G_DEBUG", "fatal-criticals", TRUE);
	(void)g_setenv("FWUPD_FUZZER_RUNNING", "1", TRUE);
	fuzzer_ctx =
	    g_object_new(FU_TYPE_CONTEXT,
			 "flags",
			 (guint64)(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS),
			 NULL);
	return 0;
}

int
LLVMFuzzerTestOneInput(const guint8 *data, gsize size)
{
	g_autoptr(FuAmdAfcState) state = NULL;

	if (size == 0)
		return 0;
	state = fu_amd_afc_state_new(fuzzer_ctx);
	if (size >= 4 && memcmp(data, "SSDT", 4) == 0) {
		fu_fuzzer_table(state, data, size);
		return 0;
	}

	switch (data[0] % 5) {
	case 0:
		fu_fuzzer_table(state, data + 1, size - 1);
		break;
	case 1:
		fu_fuzzer_strings(state, data + 1, size - 1);
		break;
	case 2:
		fu_fuzzer_forms(state, data + 1, size - 1);
		break;
	case 3:
		(void)fu_amd_afc_hii_parse_varstores(state, data + 1, size - 1, NULL);
		break;
	case 4:
		fu_fuzzer_config(data + 1, size - 1);
		break;
	default:
		g_assert_not_reached();
	}
	return 0;
}
