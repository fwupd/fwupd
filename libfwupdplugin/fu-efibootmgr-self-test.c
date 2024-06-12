/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#define G_LOG_DOMAIN "FuEfibootmgrSelfTest"

#include <fwupdplugin.h>

#include "fu-dummy-efivars.h"

typedef struct {
	FuEfivars *efivars;
	const gchar *libespdir;
	const gchar *espdir;
	const gchar *fn_shim;
	GBytes *blob_shim;
} FuEfibootmgrContext;

static gboolean
fu_efibootmgr_cmd_install_as_grub(FuEfibootmgrContext *ctx, GBytes *blob_grub, GError **error)
{
	// ESP / Boot0001 / {shim.efi + grub.efi}(GRUB) / ESP / Boot0002 /
	//    {shim.efi + grub.efi}(OLD GRUB) / ESP / Boot0003 / {shim.efi + grub.efi}(NMBL) / ESP /
	//    Boot0004 /
	//    {shim.efi + grub.efi}(OLD NMBL)We just installed a new nmbl : So we set BootNext =
	//    0003 What should BootOrder be ?
	//	g_autofree gchar *basename = g_path_get_basename(fn);
	//	g_autofree gchar *basename_b = fu_efibootmgr_convert_basename_b(basename);
	//	g_autoptr(GFile) file_esp = g_file_new_build_filename(espdir, basename, NULL);
	//	g_autoptr(GFile) file_esp_b = g_file_new_build_filename(espdir, basename_b, NULL);

	/* copy ESP/shim.efi to ESP/shim_b.efi if it exists */
	//	if (g_file_query_exists(file_esp, NULL)) {
	//		g_debug("copying %s to %s",
	//			g_file_peek_path(file_esp),
	//			g_file_peek_path(file_esp_b));
	//		if (!g_file_copy(file_esp,
	//				 file_esp_b,
	//				 G_FILE_COPY_OVERWRITE,
	//				 NULL,
	//				 NULL,
	//				 NULL,
	//				 error))
	//			return FALSE;
	//	}
}

static gboolean
fu_efibootmgr_cmd_install(FuEfivars *efivars,
			  const gchar *libespdir,
			  const gchar *espdir,
			  GError **error)
{
	g_autoptr(GPtrArray) fns = NULL;
	const gchar *fn_shim;
	g_autofree FuEfibootmgrContext *ctx = g_new0(FuEfibootmgrContext, 1); // needs AUTOPTR

	fns = fu_path_glob(libespdir, "*.efi", error);
	if (fns == NULL) {
		g_prefix_error(error, "no files installed in %s: ", libespdir);
		return FALSE;
	}

	/* look for shim */
	ctx->efivars = efivars;
	ctx->libespdir = libespdir;
	ctx->espdir = espdir;
	for (guint i = 0; i < fns->len; i++) {
		const gchar *fn = g_ptr_array_index(fns, i);
		if (g_pattern_match_simple("*/shim*.efi", fn)) {
			if (fn_shim != NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "more than one shim in %s",
					    libespdir);
				return FALSE;
			}
			fn_shim = fn;
		}
	}
	if (fn_shim == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "could not find shim in %s",
			    libespdir);
		return FALSE;
	}
	ctx->blob_shim = fu_bytes_get_contents(fn_shim, error);
	if (ctx->blob_shim == NULL)
		return FALSE;

	/* everything that isn't shim */
	for (guint i = 0; i < fns->len; i++) {
		const gchar *fn = g_ptr_array_index(fns, i);
		g_autoptr(GBytes) blob_grub = NULL;
		if (fn == fn_shim)
			continue;
		g_warning("installing as grub: %s", fn);
		blob_grub = fu_bytes_get_contents(fn, error);
		if (blob_grub == NULL)
			return FALSE;
		if (!fu_efibootmgr_cmd_install_as_grub(ctx, blob_grub, error))
			return FALSE;
	}
	// if BootCurrent!=shim_b.efi:
	//	copy ESP/shim.efi to ESP/shim_b.efi
	//	if not fallback BootXXXX exists:
	//		create a new fedora fallback BootXXXX entry
	//	if BootOrder != shim_b.efi,shim.efi
	//		set BootOrder shim_b.efi,shim.efi
	// copy lib/shim.efi to ESP/shim.efi
	// set BootNext = ESP/shim.efi
	return TRUE;
}

static gboolean
fu_efibootmgr_cmd_booted(FuEfivars *efivars, GError **error)
{
	// if BootCurrent == shim_b:
	//	log obnoxious warning
	//	return
	// is BootOrder != shim.efi,shim_b.efi:
	//	set BootOrder=shim.efi,shim_b.efi
	// #endif
	return TRUE;
}

static gboolean
_file_exists(FuEfivars *efivars, const gchar *filename)
{
	return TRUE;
}

typedef struct {
	gchar *name;
} FuEfibootmgrEntry;

static FuEfibootmgrEntry *
_get_entry(FuEfivars *efivars, const gchar *guid, const gchar *name, GError **error)
{
	return NULL;
}

/* of FuEfibootmgrEntry */
static GPtrArray *
_get_entries(FuEfivars *efivars, const gchar *guid, const gchar *name, GError **error)
{
	return NULL;
}

static GArray *
_get_uint16s(FuEfivars *efivars, const gchar *guid, const gchar *name, GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	GArray *array = g_array_new(TRUE, TRUE, sizeof(guint16));

	blob = fu_efivars_get_data_bytes(efivars, guid, name, NULL, error);
	if (blob == NULL)
		return NULL;
	for (gsize i = 0; i < g_bytes_get_size(blob); i += 2) {
		guint16 num = *((guint16 *)g_bytes_get_data(blob, NULL) + i);
		g_array_append_val(array, num);
	}
	return array;
}

static void
fu_efibootmgr_factory_func(void)
{
	gboolean ret;
	const gchar *libespdir = "/tmp/lib/esp.d";
	const gchar *espdir = "/tmp/ESP";
	g_autofree gchar *libdir_shim = NULL;
	g_autofree gchar *esp_shim = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GArray) u16_bootnext = NULL;
	g_autoptr(GArray) u16_bootorder = NULL;
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
	FuEfibootmgrEntry *entry;

	/*
	 * START:
	 * Boot0000=
	 * BootNext=
	 * BootOrder=
	 */
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Boot0000"));
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootNext"));
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootOrder"));

	/*
	 * anaconda installs shim
	 * anaconda installs grub
	 * anaconda sets up the ESP
	 * anaconda runs `efibootmgr --install` as posttrans
	 */
	libdir_shim = g_build_filename(libespdir, "shim.efi", NULL);
	ret = fu_path_mkdir_parent(libdir_shim, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(libdir_shim, "shim", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	esp_shim = g_build_filename(espdir, "shim.efi", NULL);
	ret = fu_path_mkdir_parent(esp_shim, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efibootmgr_cmd_install(efivars, libespdir, espdir, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/*
	 * POST:
	 * 1 new shim in lib/shim.efi
	 * 1 new shim in ESP/Boot0000/shim.efi
	 * 1 new shim in ESP/Boot0000/grub.efi
	 * Boot0000=shim.efi
	 * BootNext=0000
	 * BootOrder=0000
	 */
	g_assert_true(g_file_test(libdir_shim, G_FILE_TEST_EXISTS));
	g_assert_true(g_file_test(esp_shim, G_FILE_TEST_EXISTS));
	entry = _get_entry(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "Boot0000", &error);
	g_assert_no_error(error);
	g_assert_nonnull(entry);
	g_assert_cmpstr(entry->name, ==, "shim.efi");

	u16_bootnext = _get_uint16s(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootNext", &error);
	g_assert_no_error(error);
	g_assert_nonnull(u16_bootnext);
	g_assert_cmpint(u16_bootnext->len, ==, 1);
	g_assert_cmpint(g_array_index(u16_bootnext, guint16, 0), ==, 0);
	u16_bootorder = _get_uint16s(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootOrder", &error);
	g_assert_no_error(error);
	g_assert_nonnull(u16_bootorder);
	g_assert_cmpint(u16_bootorder->len, ==, 1);
	g_assert_cmpint(g_array_index(u16_bootorder, guint16, 0), ==, 0);

	/* reboot */
	ret = fu_efivars_delete(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootNext", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efibootmgr_cmd_booted(efivars, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/*
	 * END:
	 * Boot0000=shim.efi
	 * BootNext=
	 * BootOrder=0000
	 */
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootNext"));
	g_assert_true(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootOrder"));

	/* install the exact same things, and we expect it to be ignored
	 *
	 * Boot0000=shim.efi
	 * BootNext=
	 * BootOrder=0000
	 */
	ret = fu_efibootmgr_cmd_install(efivars, libespdir, espdir, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootNext"));
	g_assert_true(fu_efivars_exists(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootOrder"));
}

static void
fu_efibootmgr_freshfu_efibootmgr_cmd_install_func(void)
{
	gboolean ret;
	g_autoptr(FuEfivars) efivars = fu_dummy_efivars_new();
	g_autoptr(GError) error = NULL;

	/*
	 * START:
	 * 1 old lib/shim.efi
	 * 1 old ESP/shim.efi
	 * BootNext=
	 * BootOrder=shim.efi
	 */

	/*
	 * rpm installs new shim to lib/shim.efi, then calls efibootmgr --install
	 */
	//	ret = fu_efibootmgr_cmd_install(efivars, &error);
	//	g_assert_no_error(error);
	//	g_assert_true(ret);

	/*
	 * POST:
	 * 1 new shim in lib/shim.efi
	 * 1 old shim in ESP/shim_b.efi
	 * 1 new shim in ESP/shim.efi
	 * BootNext=shim.efi
	 * BootOrder=shim_b.efi,shim.efi
	 */
	ret = fu_efivars_delete(efivars, FU_EFIVARS_GUID_EFI_GLOBAL, "BootNext", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efibootmgr_cmd_booted(efivars, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/*
	 * END:
	 * 1 new shim in lib/shim.efi
	 * 1 old shim in ESP/shim_b.efi
	 * 1 new shim in ESP/shim.efi
	 * BootNext=
	 * BootOrder=shim.efi,shim_b.efi
	 */
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	g_test_add_func("/fwupd/efibootmgr{factory}", fu_efibootmgr_factory_func);
	g_test_add_func("/fwupd/efibootmgr{fresh-install}",
			fu_efibootmgr_freshfu_efibootmgr_cmd_install_func);
	return g_test_run();
}
