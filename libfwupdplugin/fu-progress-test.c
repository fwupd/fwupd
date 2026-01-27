/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-progress-private.h"

typedef struct {
	guint last_percentage;
	guint updates;
} FuProgressHelper;

static void
fu_progress_percentage_changed_cb(FuProgress *progress, guint percentage, gpointer data)
{
	FuProgressHelper *helper = (FuProgressHelper *)data;
	helper->last_percentage = percentage;
	helper->updates++;
}

static void
fu_progress_func(void)
{
	FuProgressHelper helper = {0};
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autofree gchar *str = NULL;

	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	g_assert_cmpfloat_with_epsilon(fu_progress_get_duration(progress), 0.f, 0.001);

	fu_progress_set_profile(progress, TRUE);
	fu_progress_set_steps(progress, 5);
	g_assert_cmpint(helper.last_percentage, ==, 0);

	g_usleep(20 * 1000);
	fu_progress_step_done(progress);
	g_assert_cmpint(helper.updates, ==, 2);
	g_assert_cmpint(helper.last_percentage, ==, 20);

	for (guint i = 0; i < 4; i++) {
		g_usleep(20 * 1000);
		fu_progress_step_done(progress);
	}

	g_assert_cmpint(helper.last_percentage, ==, 100);
	g_assert_cmpint(helper.updates, ==, 6);
	g_assert_cmpfloat_with_epsilon(fu_progress_get_duration(progress), 0.1f, 0.05);
	str = fu_progress_traceback(progress);
	g_debug("%s", str);
}

static void
fu_progress_child_func(void)
{
	FuProgressHelper helper = {0};
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* reset */
	fu_progress_set_profile(progress, TRUE);
	fu_progress_set_steps(progress, 2);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	/* parent: |-----------------------|-----------------------|
	 * step1:  |-----------------------|
	 * child:                          |-------------|---------|
	 */

	/* PARENT UPDATE */
	g_debug("parent update #1");
	fu_progress_step_done(progress);
	g_assert_cmpint(helper.updates, ==, 1);
	g_assert_cmpint(helper.last_percentage, ==, 50);

	/* now test with a child */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);

	g_debug("child update #1");
	fu_progress_step_done(child);
	g_assert_cmpint(helper.updates, ==, 2);
	g_assert_cmpint(helper.last_percentage, ==, 75);

	/* child update */
	g_debug("child update #2");
	fu_progress_step_done(child);
	g_assert_cmpint(helper.updates, ==, 3);
	g_assert_cmpint(helper.last_percentage, ==, 100);

	/* parent update */
	g_debug("parent update #2");
	fu_progress_step_done(progress);

	/* ensure we ignored the duplicate */
	g_assert_cmpint(helper.updates, ==, 3);
	g_assert_cmpint(helper.last_percentage, ==, 100);
}

static void
fu_progress_scaling_func(void)
{
	const guint insane_steps = 1000 * 1000;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	fu_progress_set_steps(progress, insane_steps);
	for (guint i = 0; i < insane_steps / 2; i++)
		fu_progress_step_done(progress);
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 50);
	for (guint i = 0; i < insane_steps / 2; i++) {
		FuProgress *progress_child = fu_progress_get_child(progress);
		fu_progress_set_percentage(progress_child, 0);
		fu_progress_set_percentage(progress_child, 100);
		fu_progress_step_done(progress);
	}
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 100);
}

static void
fu_progress_parent_one_step_proxy_func(void)
{
	FuProgressHelper helper = {0};
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* one step */
	fu_progress_set_steps(progress, 1);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	/* now test with a child */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);

	/* child set value */
	fu_progress_set_percentage(child, 33);

	/* ensure 1 updates for progress with one step and ensure using child value as parent */
	g_assert_cmpint(helper.updates, ==, 1);
	g_assert_cmpint(helper.last_percentage, ==, 33);
}

static void
fu_progress_non_equal_steps_func(void)
{
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	FuProgress *child;
	FuProgress *grandchild;

	/* test non-equal steps */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 60, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 20, NULL);
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 0);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_ERASE);

	/* child step should increment according to the custom steps */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);
	fu_progress_set_status(child, FWUPD_STATUS_DEVICE_BUSY);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_BUSY);

	/* start child */
	fu_progress_step_done(child);

	/* verify 10% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 10);

	/* finish child */
	fu_progress_step_done(child);

	/* ensure the parent is switched back to the status before the child took over */
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_ERASE);

	fu_progress_step_done(progress);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_WRITE);

	/* verify 20% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 20);

	/* child step should increment according to the custom steps */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_add_step(child, FWUPD_STATUS_DEVICE_RESTART, 25, NULL);
	fu_progress_add_step(child, FWUPD_STATUS_DEVICE_WRITE, 75, NULL);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_RESTART);

	/* start child */
	fu_progress_step_done(child);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_WRITE);

	/* verify bilinear interpolation is working */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 35);

	/*
	 * 0        20                             80         100
	 * |---------||----------------------------||---------|
	 *            |       35                   |
	 *            |-------||-------------------| (25%)
	 *                     |              75.5 |
	 *                     |---------------||--| (90%)
	 */
	grandchild = fu_progress_get_child(child);
	fu_progress_set_id(grandchild, G_STRLOC);
	fu_progress_add_step(grandchild, FWUPD_STATUS_DEVICE_ERASE, 90, NULL);
	fu_progress_add_step(grandchild, FWUPD_STATUS_DEVICE_WRITE, 10, NULL);

	fu_progress_step_done(grandchild);

	/* verify bilinear interpolation (twice) is working for subpercentage */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 75);

	fu_progress_step_done(grandchild);

	/* finish child */
	fu_progress_step_done(child);

	fu_progress_step_done(progress);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_READ);

	/* verify 80% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 80);

	fu_progress_step_done(progress);

	/* verify 100% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 100);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_UNKNOWN);
}

static void
fu_progress_finish_func(void)
{
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* check straight finish */
	fu_progress_set_steps(progress, 3);

	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 3);
	fu_progress_finished(child);

	/* parent step done after child finish */
	fu_progress_step_done(progress);
}

static void
fu_progress_global_fraction_func(void)
{
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* sanity check */
	fu_progress_set_steps(progress, 100);
	g_assert_cmpfloat_with_epsilon(fu_progress_get_global_fraction(progress), 1.f, 0.001);

	/* 1% */
	child = fu_progress_get_child(progress);
	g_assert_cmpfloat_with_epsilon(fu_progress_get_global_fraction(child), 0.01f, 0.001);

	/* 0.01% */
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 100);
	fu_progress_step_done(child);
	fu_progress_step_done(child);
	fu_progress_step_done(child);
	fu_progress_finished(child);

	/* done */
	fu_progress_finished(progress);
}

static void
fu_progress_child_finished(void)
{
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* check straight finish */
	fu_progress_set_steps(progress, 3);

	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 3);
	/* some imaginary ignorable error */

	/* parent step done after child finish */
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_CHILD_FINISHED);
	fu_progress_step_done(progress);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	if (g_test_slow())
		g_test_add_func("/fwupd/progress", fu_progress_func);
	g_test_add_func("/fwupd/progress/scaling", fu_progress_scaling_func);
	g_test_add_func("/fwupd/progress/child", fu_progress_child_func);
	g_test_add_func("/fwupd/progress/child-finished", fu_progress_child_finished);
	g_test_add_func("/fwupd/progress/parent-1-step", fu_progress_parent_one_step_proxy_func);
	g_test_add_func("/fwupd/progress/no-equal", fu_progress_non_equal_steps_func);
	g_test_add_func("/fwupd/progress/finish", fu_progress_finish_func);
	g_test_add_func("/fwupd/progress/global-fraction", fu_progress_global_fraction_func);
	return g_test_run();
}
