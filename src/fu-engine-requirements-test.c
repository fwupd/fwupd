/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-remote-private.h"

#include "fu-context-private.h"
#include "fu-engine-requirements.h"
#include "fu-engine.h"

static void
fu_engine_requirements_missing_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <id compare=\"ge\" version=\"1.2.3\">not.going.to.exist</id>"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_add_runtime_version(engine, "org.test.dummy", "1.2.3");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_engine_requirements_soft_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <suggests>"
			   "    <id compare=\"ge\" version=\"1.2.3\">not.going.to.exist</id>"
			   "  </suggests>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_add_runtime_version(engine, "org.test.dummy", "1.2.3");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine,
					   release,
					   FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS,
					   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_client_fail_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <client>detach-action</client>"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_engine_requirements_client_invalid_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <client>hello-dave</client>"
			   "    <id compare=\"ge\" version=\"1.4.5\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_engine_requirements_client_pass_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <client>detach-action</client>"
			   "    <id compare=\"ge\" version=\"1.4.5\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_request_set_feature_flags(request, FWUPD_FEATURE_FLAG_DETACH_ACTION);

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_vercmp_glob_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <client>id-requirement-glob</client>"
			   "    <id compare=\"ge\" "
			   "version=\"1.8.*=1.8.5|1.9.*=1.9.7|2.0.13\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* hardcode to specific branch */
	fu_context_add_runtime_version(ctx, "org.freedesktop.fwupd", "1.9.8");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* reset back to reality */
	fu_context_add_runtime_version(ctx, "org.freedesktop.fwupd", VERSION);
}

static void
fu_engine_requirements_vercmp_glob_fallback_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <id compare=\"ge\" "
			   "version=\"1.8.*=1.8.5|1.9.*=1.9.7|2.0.13\">org.freedesktop.fwupd</id>\n"
			   "    <client>id-requirement-glob</client>"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_not_hardware_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <not_hardware>ffffffff-ffff-ffff-ffff-ffffffffffff</not_hardware>"
			   "    <id compare=\"ge\" version=\"1.9.10\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_request_set_feature_flags(request, FWUPD_FEATURE_FLAG_DETACH_ACTION);

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_phased_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <phased_update>10</phased_update>"
			   "    <id compare=\"ge\" version=\"2.0.17\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* do not include into seed */
	g_assert_cmpstr(fu_engine_get_host_machine_id(engine), ==, NULL);

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	fu_release_set_remote(release, remote);
	fwupd_remote_set_mtime(remote, 12340);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check this still passes as we're ignoring */
	fwupd_remote_set_mtime(remote, 12345);
	ret = fu_engine_requirements_check(engine,
					   release,
					   FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS,
					   &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check this now fails */
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* user disabled this */
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_NO_PHASED_UPDATES);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_phased_old_fwupd_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <phased_update>10</phased_update>"
			   "    <id compare=\"ge\" version=\"2.0.16\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* do not include into seed */
	g_assert_cmpstr(fu_engine_get_host_machine_id(engine), ==, NULL);

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	fu_release_set_remote(release, remote);
	fwupd_remote_set_mtime(remote, 12340);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check this fails because the fwupd requirement is too low */
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_engine_requirements_version_require_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_bootloader(device, "4.5.6");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_true(
	    g_str_has_prefix(error->message, "device requires firmware with a version check"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_version_lowest_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.2\">"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_lowest(device, "1.2.3");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_release_check_version(release, component, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_true(
	    g_str_has_prefix(error->message, "Specified firmware is older than the minimum"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_unsupported_func(void)
{
	gboolean ret;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <UNKNOWN compare=\"ge\" version=\"2.6.0\"/>"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up a dummy version */
	fu_engine_add_runtime_version(engine, "org.test.dummy", "1.2.3");

	/* make the component require one thing that we don't support */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_engine_requirements_child_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware compare=\"eq\" version=\"0.0.1\">not-child</firmware>"
	    "    <id compare=\"ge\" version=\"1.2.11\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_bootloader(device, "4.5.6");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(child, "0.0.999");
	fu_device_set_physical_id(child, "dummy");
	fu_device_add_child(device, child);

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_child_fail_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware compare=\"glob\" version=\"0.0.*\">not-child</firmware>"
	    "    <id compare=\"ge\" version=\"1.2.11\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_bootloader(device, "4.5.6");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(child, "0.0.1");
	fu_device_set_physical_id(child, "dummy");
	fu_device_add_child(device, child);

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(
	    g_strstr_len(error->message, -1, "Not compatible with child device version"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml = "<component>"
			   "  <requires>"
			   "    <id compare=\"ge\" version=\"1.2.3\">org.test.dummy</id>"
			   "    <hardware>6ff95c9c-ae41-5f59-9d90-3ec1ea66091e</hardware>"
			   "    <id compare=\"ge\" version=\"1.0.1\">org.freedesktop.fwupd</id>\n"
			   "    <id compare=\"ge\" version=\"1.9.10\">org.freedesktop.fwupd</id>\n"
			   "  </requires>"
			   "  <releases>"
			   "    <release version=\"1.2.3\"/>"
			   "  </releases>"
			   "</component>";

	/* set up some dummy versions */
	fu_engine_add_runtime_version(engine, "org.test.dummy", "1.2.3");
	fu_engine_add_runtime_version(engine, "com.hughski.colorhug", "7.8.9");

	/* make the component require one thing */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware compare=\"ge\" version=\"1.2.3\"/>"
	    "    <firmware compare=\"eq\" version=\"4.5.6\">bootloader</firmware>"
	    "    <firmware compare=\"regex\" version=\"USB:0xFFFF|DMI:Lenovo\">vendor-id</firmware>"
#ifdef __linux__
	    "    <id compare=\"ge\" version=\"4.0.0\">org.kernel</id>"
#endif
	    "    <id compare=\"ge\" version=\"1.2.11\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "1.2.3");
	fu_device_set_version_bootloader(device, "4.5.6");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_build_vendor_id_u16(device, "PCI", 0x0000);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check this fails, as the wrong requirement is specified */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(g_strstr_len(error->message, -1, "child, parent or sibling requirement"));
	g_assert_false(ret);

#ifndef SUPPORTED_BUILD
	/* we can force this */
	g_clear_error(&error);
	ret = fu_engine_requirements_check(engine,
					   release,
					   FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS,
					   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
#endif
}

static void
fu_engine_requirements_device_plain_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"51H0AALB\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "      <checksum type=\"sha1\" filename=\"bios.cab\" target=\"container\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_version(device, "5101AALB");
	fu_device_build_vendor_id_u16(device, "USB", 0xFFFF);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_release_get_filename(release), ==, "bios.cab");
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_version_format_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "  <custom>"
	    "    <value key=\"LVFS::VersionFormat\">triplet</value>"
	    "  </custom>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version(device, "1.2.3.4");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_release_check_version(release, component, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(
	    g_strstr_len(error->message, -1, "Firmware version formats were different"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_only_upgrade_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.3\"/>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version(device, "1.2.4");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_release_check_version(release, component, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(g_strstr_len(error->message, -1, "Device only supports version upgrades"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_only_upgrade_reinstall_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.3\"/>"
	    "  </releases>"
	    "</component>";

	/* set up a dummy device */
	fu_device_set_version(device, "1.2.3");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);
	fu_device_add_instance_id(device, "12345678-1234-1234-1234-123456789012");

	/* make the component require three things */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release, device);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_release_check_version(release, component, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(g_strstr_len(error->message, -1, "Device only supports version upgrades"));
	g_assert_false(ret);
}

static void
fu_engine_requirements_sibling_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDevice) unrelated_device3 = fu_device_new(ctx);
	g_autoptr(FuDevice) parent = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release1 = fu_release_new();
	g_autoptr(FuRelease) release2 = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware depth=\"0\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
	    "    <id compare=\"ge\" version=\"1.6.1\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up a dummy device */
	fu_device_set_id(device1, "id1");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_protocol(device1, "com.acme");
	fu_engine_add_device(engine, device1);

	/* setup the parent */
	fu_device_set_id(parent, "parent");
	fu_device_set_version_format(parent, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(parent, "1.0.0");
	fu_device_add_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(parent, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(parent, "42f3d696-0b6f-4d69-908f-357f98ef115e");
	fu_device_add_protocol(parent, "com.acme");
	fu_device_add_child(parent, device1);
	fu_engine_add_device(engine, parent);

	/* set up a different device */
	fu_device_set_id(unrelated_device3, "id3");
	fu_device_build_vendor_id(unrelated_device3, "USB", "FFFF");
	fu_device_add_protocol(unrelated_device3, "com.acme");
	fu_device_set_name(unrelated_device3, "Foo bar device");
	fu_device_set_version_format(unrelated_device3, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(unrelated_device3, "1.5.3");
	fu_device_add_flag(unrelated_device3, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(unrelated_device3, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(unrelated_device3, "3e455c08-352e-4a16-84d3-f04287289fa2");
	fu_engine_add_device(engine, unrelated_device3);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release1, device1);
	fu_release_set_request(release1, request);
	ret = fu_release_load(release1, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release1, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* set up a sibling device */
	fu_device_set_id(device2, "id2");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_set_name(device2, "Secondary firmware");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "4.5.6");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");
	fu_device_add_child(parent, device2);
	fu_engine_add_device(engine, device2);

	/* check this passes */
	fu_release_set_device(release2, device1);
	fu_release_set_request(release2, request);
	ret = fu_release_load(release2, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release2, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check this still works, as a child requirement is specified */
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
	ret = fu_engine_requirements_check(engine, release2, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_other_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware compare=\"gt\" "
	    "version=\"4.0.0\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
	    "    <id compare=\"ge\" version=\"1.2.11\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"1.2.4\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up a dummy device */
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device1, "12345678-1234-1234-1234-123456789012");

	/* set up a different device */
	fu_device_set_id(device2, "id2");
	fu_device_build_vendor_id_u16(device2, "USB", 0xFFFF);
	fu_device_add_protocol(device2, "com.acme");
	fu_device_set_name(device2, "Secondary firmware");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "4.5.6");
	fu_device_add_instance_id(device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");
	fu_engine_add_device(engine, device2);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device1);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_protocol_check_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FuRelease) release1 = fu_release_new();
	g_autoptr(FuRelease) release2 = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	gboolean ret;

	const gchar *xml =
	    "<component>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"4.5.7\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "  <custom>"
	    "    <value key=\"LVFS::UpdateProtocol\">org.bar</value>"
	    "  </custom>"

	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	fu_device_set_id(device1, "NVME");
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_name(device1, "NVME device");
	fu_device_build_vendor_id(device1, "DMI", "ACME");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_instance_id(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device1);

	fu_device_set_id(device2, "UEFI");
	fu_device_add_protocol(device2, "org.bar");
	fu_device_set_name(device2, "UEFI device");
	fu_device_build_vendor_id(device2, "DMI", "ACME");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "1.2.3");
	fu_device_add_instance_id(device2, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_engine_add_device(engine, device2);

	/* make sure both devices added */
	devices = fu_engine_get_devices(engine, &error);
	g_assert_no_error(error);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 2);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this fails */
	fu_release_set_device(release1, device1);
	fu_release_set_request(release1, request);
	ret = fu_release_load(release1, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* check this passes */
	fu_release_set_device(release2, device2);
	fu_release_set_request(release2, request);
	ret = fu_release_load(release2, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_parent_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware depth=\"1\" compare=\"eq\" version=\"1.2.3\"/>"
	    "    <firmware depth=\"1\">12345678-1234-1234-1234-123456789012</firmware>"
	    "    <id compare=\"ge\" version=\"1.3.4\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"4.5.7\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up child device */
	fu_device_set_id(device2, "child");
	fu_device_set_name(device2, "child");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "4.5.6");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");

	/* set up a parent device */
	fu_device_set_id(device1, "parent");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_name(device1, "parent");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_instance_id(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_child(device1, device2);
	fu_engine_add_device(engine, device1);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device2);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_engine_requirements_child_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuEngineRequest) request = fu_engine_request_new(NULL);
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbSilo) silo_empty = xb_silo_new();
	const gchar *xml =
	    "<component>"
	    "  <requires>"
	    "    <firmware depth=\"-1\">1ff60ab2-3905-06a1-b476-0371f00c9e9b</firmware>"
	    "    <id compare=\"ge\" version=\"1.9.7\">org.freedesktop.fwupd</id>\n"
	    "  </requires>"
	    "  <provides>"
	    "    <firmware type=\"flashed\">12345678-1234-1234-1234-123456789012</firmware>"
	    "  </provides>"
	    "  <releases>"
	    "    <release version=\"4.5.7\">"
	    "      <checksum type=\"sha1\" filename=\"bios.bin\" target=\"content\"/>"
	    "    </release>"
	    "  </releases>"
	    "</component>";

	/* no metadata in daemon */
	fu_engine_set_silo(engine, silo_empty);

	/* set up a parent device */
	fu_device_set_id(device1, "parent");
	fu_device_build_vendor_id_u16(device1, "USB", 0xFFFF);
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_name(device1, "parent");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_instance_id(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	/* set up child device */
	fu_device_set_id(device2, "child");
	fu_device_set_name(device2, "child");
	fu_device_set_version_format(device2, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device2, "4.5.6");
	fu_device_add_instance_id(device2, "1ff60ab2-3905-06a1-b476-0371f00c9e9b");
	fu_device_add_child(device1, device2);
	fu_engine_add_device(engine, device1);

	/* import firmware metainfo */
	silo = xb_silo_new_from_xml(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	component = xb_silo_query_first(silo, "component", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);

	/* check this passes */
	fu_release_set_device(release, device1);
	fu_release_set_request(release, request);
	ret = fu_release_load(release, NULL, component, NULL, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_engine_requirements_check(engine, release, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/engine/requirements", fu_engine_requirements_func);
	g_test_add_func("/fwupd/engine/requirements/soft", fu_engine_requirements_soft_func);
	g_test_add_func("/fwupd/engine/requirements/missing", fu_engine_requirements_missing_func);
	g_test_add_func("/fwupd/engine/requirements/client-fail",
			fu_engine_requirements_client_fail_func);
	g_test_add_func("/fwupd/engine/requirements/client-invalid",
			fu_engine_requirements_client_invalid_func);
	g_test_add_func("/fwupd/engine/requirements/client-pass",
			fu_engine_requirements_client_pass_func);
	g_test_add_func("/fwupd/engine/requirements/not-hardware",
			fu_engine_requirements_not_hardware_func);
	g_test_add_func("/fwupd/engine/requirements/phased", fu_engine_requirements_phased_func);
	g_test_add_func("/fwupd/engine/requirements/phased-old-fwpud",
			fu_engine_requirements_phased_old_fwupd_func);
	g_test_add_func("/fwupd/engine/requirements/version-require",
			fu_engine_requirements_version_require_func);
	g_test_add_func("/fwupd/engine/requirements/version-lowest",
			fu_engine_requirements_version_lowest_func);
	g_test_add_func("/fwupd/engine/requirements/parent-device",
			fu_engine_requirements_parent_device_func);
	g_test_add_func("/fwupd/engine/requirements/child-device",
			fu_engine_requirements_child_device_func);
	g_test_add_func("/fwupd/engine/requirements_protocol_check_func",
			fu_engine_requirements_protocol_check_func);
	g_test_add_func("/fwupd/engine/requirements/not-child", fu_engine_requirements_child_func);
	g_test_add_func("/fwupd/engine/requirements/not-child-fail",
			fu_engine_requirements_child_fail_func);
	g_test_add_func("/fwupd/engine/requirements/unsupported",
			fu_engine_requirements_unsupported_func);
	g_test_add_func("/fwupd/engine/requirements/device", fu_engine_requirements_device_func);
	g_test_add_func("/fwupd/engine/requirements/device-plain",
			fu_engine_requirements_device_plain_func);
	g_test_add_func("/fwupd/engine/requirements/version-format",
			fu_engine_requirements_version_format_func);
	g_test_add_func("/fwupd/engine/requirements/only-upgrade",
			fu_engine_requirements_only_upgrade_func);
	g_test_add_func("/fwupd/engine/requirements/only-upgrade-reinstall",
			fu_engine_requirements_only_upgrade_reinstall_func);
	g_test_add_func("/fwupd/engine/requirements/other-device",
			fu_engine_requirements_other_device_func);
	g_test_add_func("/fwupd/engine/requirements/sibling-device",
			fu_engine_requirements_sibling_device_func);
	g_test_add_func("/fwupd/engine/requirements/vercmp-glob",
			fu_engine_requirements_vercmp_glob_func);
	g_test_add_func("/fwupd/engine/requirements/vercmp-glob-fallback",
			fu_engine_requirements_vercmp_glob_fallback_func);
	return g_test_run();
}
