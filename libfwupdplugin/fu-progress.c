/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuProgress"

#include "config.h"

#include <math.h>

#include "fu-progress.h"
#include "fu-string.h"

/**
 * FuProgress:
 *
 * Objects can use fu_progress_set_percentage() if the absolute percentage
 * is known. Percentages should always go up, not down.
 *
 * Modules usually set the number of steps that are expected using
 * fu_progress_set_steps() and then after each section is completed,
 * the fu_progress_step_done() function should be called. This will automatically
 * call fu_progress_set_percentage() with the correct values.
 *
 * #FuProgress allows sub-modules to be "chained up" to the parent module
 * so that as the sub-module progresses, so does the parent.
 * The child can be reused for each section, and chains can be deep.
 *
 * To get a child object, you should use [method@FuProgress.get_child]. and then
 * use the result in any sub-process. You should ensure that the child
 * is not re-used without calling fu_progress_step_done().
 *
 * There are a few nice touches in this module, so that if a module only has
 * one progress step, the child progress is used for parent updates.
 *
 *    static void
 *    _do_something(FuProgress *self)
 *    {
 *       // setup correct number of steps
 *       fu_progress_set_steps(self, 2);
 *
 *       // run a sub function
 *       _do_something_else1(fu_progress_get_child(self));
 *
 *       // this section done
 *       fu_progress_step_done(self);
 *
 *       // run another sub function
 *       _do_something_else2(fu_progress_get_child(self));
 *
 *       // this progress done (all complete)
 *       fu_progress_step_done(self);
 *    }
 *
 * See also: [class@FuDevice]
 */

struct _FuProgress {
	GObject parent_instance;
	gchar *id;
	gchar *name;
	FuProgressFlags flags;
	guint percentage;
	FwupdStatus status;
	GPtrArray *children; /* of FuProgress */
	gboolean profile;
	gdouble duration; /* seconds */
	guint step_weighting;
	GTimer *timer;
	GTimer *timer_child;
	guint step_now;
	FuProgress *parent; /* no-ref */
};

enum { SIGNAL_PERCENTAGE_CHANGED, SIGNAL_STATUS_CHANGED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE(FuProgress, fu_progress, G_TYPE_OBJECT)

/**
 * fu_progress_get_id:
 * @self: a #FuProgress
 *
 * Return the id of the progress, which is normally set by the caller.
 *
 * Returns: progress ID
 *
 * Since: 1.7.0
 **/
const gchar *
fu_progress_get_id(FuProgress *self)
{
	g_return_val_if_fail(FU_IS_PROGRESS(self), NULL);
	return self->id;
}

/**
 * fu_progress_set_id:
 * @self: a #FuProgress
 * @id: progress ID, normally `G_STRLOC`
 *
 * Sets the id of the progress.
 *
 * Since: 1.7.0
 **/
void
fu_progress_set_id(FuProgress *self, const gchar *id)
{
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(id != NULL);

	/* not changed */
	if (g_strcmp0(self->id, id) == 0)
		return;

	/* set id */
	g_free(self->id);
	self->id = g_strdup(id);
}

/**
 * fu_progress_get_name:
 * @self: a #FuProgress
 *
 * Return the nice name of the progress, which is normally set by the caller.
 *
 * Returns: progress nice name, e.g. `add-devices`
 *
 * Since: 1.8.2
 **/
const gchar *
fu_progress_get_name(FuProgress *self)
{
	g_return_val_if_fail(FU_IS_PROGRESS(self), NULL);
	return self->name;
}

static const gchar *
fu_progress_get_name_fallback(FuProgress *self)
{
	if (self->name != NULL)
		return self->name;
	return fwupd_status_to_string(self->status);
}

/**
 * fu_progress_set_name:
 * @self: a #FuProgress
 * @name: progress nice name, e.g. `add-devices`, or perhaps just `G_STRFUNC`
 *
 * Sets the nice name of the progress.
 *
 * Since: 1.8.2
 **/
void
fu_progress_set_name(FuProgress *self, const gchar *name)
{
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(name != NULL);

	/* not changed */
	if (g_strcmp0(self->name, name) == 0)
		return;

	/* set name */
	g_free(self->name);
	self->name = g_strdup(name);
}

/**
 * fu_progress_get_status:
 * @self: a #FuProgress
 *
 * Return the status of the progress, which is normally indirectly by fu_progress_add_step().
 *
 * Returns: status
 *
 * Since: 1.7.0
 **/
FwupdStatus
fu_progress_get_status(FuProgress *self)
{
	g_return_val_if_fail(FU_IS_PROGRESS(self), FWUPD_STATUS_UNKNOWN);
	return self->status;
}

/**
 * fu_progress_flag_to_string:
 * @flag: an internal progress flag, e.g. %FU_PROGRESS_FLAG_GUESSED
 *
 * Converts an progress flag to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.7.0
 **/
const gchar *
fu_progress_flag_to_string(FuProgressFlags flag)
{
	if (flag == FU_PROGRESS_FLAG_GUESSED)
		return "guessed";
	if (flag == FU_PROGRESS_FLAG_NO_PROFILE)
		return "no-profile";
	if (flag == FU_PROGRESS_FLAG_NO_TRACEBACK)
		return "no-traceback";
	return NULL;
}

/**
 * fu_progress_flag_from_string:
 * @flag: a string, e.g. `guessed`
 *
 * Converts a string to an progress flag.
 *
 * Returns: enumerated value
 *
 * Since: 1.7.0
 **/
FuProgressFlags
fu_progress_flag_from_string(const gchar *flag)
{
	if (g_strcmp0(flag, "guessed") == 0)
		return FU_PROGRESS_FLAG_GUESSED;
	if (g_strcmp0(flag, "no-profile") == 0)
		return FU_PROGRESS_FLAG_NO_PROFILE;
	if (g_strcmp0(flag, "no-traceback") == 0)
		return FU_PROGRESS_FLAG_NO_TRACEBACK;
	return FU_PROGRESS_FLAG_UNKNOWN;
}

/**
 * fu_progress_add_flag:
 * @self: a #FuProgress
 * @flag: an internal progress flag, e.g. %FU_PROGRESS_FLAG_GUESSED
 *
 * Adds a flag.
 *
 * Since: 1.7.0
 **/
void
fu_progress_add_flag(FuProgress *self, FuProgressFlags flag)
{
	g_return_if_fail(FU_IS_PROGRESS(self));
	self->flags |= flag;
}

/**
 * fu_progress_remove_flag:
 * @self: a #FuProgress
 * @flag: an internal progress flag, e.g. %FU_PROGRESS_FLAG_GUESSED
 *
 * Removes a flag.
 *
 * Since: 1.7.0
 **/
void
fu_progress_remove_flag(FuProgress *self, FuProgressFlags flag)
{
	g_return_if_fail(FU_IS_PROGRESS(self));
	self->flags &= ~flag;
}

/**
 * fu_progress_has_flag:
 * @self: a #FuProgress
 * @flag: an internal progress flag, e.g. %FU_PROGRESS_FLAG_GUESSED
 *
 * Tests for a flag.
 *
 * Since: 1.7.0
 **/
gboolean
fu_progress_has_flag(FuProgress *self, FuProgressFlags flag)
{
	g_return_val_if_fail(FU_IS_PROGRESS(self), FALSE);
	return (self->flags & flag) > 0;
}

/**
 * fu_progress_set_status:
 * @self: a #FuProgress
 * @status: device status
 *
 * Sets the status of the progress.
 *
 * Since: 1.7.0
 **/
void
fu_progress_set_status(FuProgress *self, FwupdStatus status)
{
	g_return_if_fail(FU_IS_PROGRESS(self));

	/* not changed */
	if (self->status == status)
		return;

	/* save */
	self->status = status;
	g_signal_emit(self, signals[SIGNAL_STATUS_CHANGED], 0, status);
}

/**
 * fu_progress_get_percentage:
 * @self: a #FuProgress
 *
 * Get the last set progress percentage.
 *
 * Return value: The percentage value, or %G_MAXUINT for error
 *
 * Since: 1.7.0
 **/
guint
fu_progress_get_percentage(FuProgress *self)
{
	g_return_val_if_fail(FU_IS_PROGRESS(self), G_MAXUINT);
	if (self->percentage == G_MAXUINT)
		return 0;
	return self->percentage;
}

static void
fu_progress_set_parent(FuProgress *self, FuProgress *parent)
{
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(FU_IS_PROGRESS(parent));
	self->parent = parent; /* no ref! */
	self->profile = fu_progress_get_profile(parent);
}

/**
 * fu_progress_get_duration:
 * @self: a #FuProgress
 *
 * Get the duration of the step.
 *
 * Return value: The duration value in seconds
 *
 * Since: 1.8.2
 **/
gdouble
fu_progress_get_duration(FuProgress *self)
{
	return self->duration;
}

static void
fu_progress_set_duration(FuProgress *self, gdouble duration)
{
	self->duration = duration;
}

static void
fu_progress_build_parent_chain(FuProgress *self, GString *str, guint level)
{
	if (self->parent != NULL)
		fu_progress_build_parent_chain(self->parent, str, level + 1);
	g_string_append_printf(str,
			       "%u) %s (%u/%u)\n",
			       level,
			       self->id,
			       self->step_now,
			       self->children->len);
}

/**
 * fu_progress_set_percentage:
 * @self: a #FuProgress
 * @percentage: value between 0% and 100%
 *
 * Sets the progress percentage complete.
 *
 * NOTE: this must be above what was previously set, or it will be rejected.
 *
 * Since: 1.7.0
 **/
void
fu_progress_set_percentage(FuProgress *self, guint percentage)
{
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(percentage <= 100);

	/* is it the same */
	if (percentage == self->percentage)
		return;

	/* is it less */
	if (self->percentage != G_MAXUINT && percentage < self->percentage) {
		if (self->profile) {
			g_autoptr(GString) str = g_string_new(NULL);
			fu_progress_build_parent_chain(self, str, 0);
			g_warning("percentage should not go down from %u to %u: %s",
				  self->percentage,
				  percentage,
				  str->str);
		}
		return;
	}

	/* done */
	if (percentage == 100) {
		fu_progress_set_duration(self, g_timer_elapsed(self->timer, NULL));
		for (guint i = 0; i < self->children->len; i++) {
			FuProgress *child = g_ptr_array_index(self->children, i);
			g_signal_handlers_disconnect_by_data(child, self);
		}
	}

	/* save */
	self->percentage = percentage;
	g_signal_emit(self, signals[SIGNAL_PERCENTAGE_CHANGED], 0, percentage);
}

/**
 * fu_progress_set_percentage_full:
 * @self: a #FuDevice
 * @progress_done: the bytes already done
 * @progress_total: the total number of bytes
 *
 * Sets the progress completion using the raw progress values.
 *
 * Since: 1.7.0
 **/
void
fu_progress_set_percentage_full(FuProgress *self, gsize progress_done, gsize progress_total)
{
	gdouble percentage = 0.f;
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(progress_done <= progress_total);
	if (progress_total > 0)
		percentage = (100.f * (gdouble)progress_done) / (gdouble)progress_total;
	fu_progress_set_percentage(self, (guint)percentage);
}

/**
 * fu_progress_set_profile:
 * @self: A #FuProgress
 * @profile: if profiling should be enabled
 *
 * This enables profiling of FuProgress. This may be useful in development,
 * but be warned; enabling profiling makes #FuProgress very slow.
 *
 * Since: 1.7.0
 **/
void
fu_progress_set_profile(FuProgress *self, gboolean profile)
{
	g_return_if_fail(FU_IS_PROGRESS(self));
	self->profile = profile;
}

/**
 * fu_progress_get_profile:
 * @self: A #FuProgress
 *
 * Returns if the profile is enabled for this progress.
 *
 * Return value: if profiling has been enabled
 *
 * Since: 1.8.2
 **/
gboolean
fu_progress_get_profile(FuProgress *self)
{
	g_return_val_if_fail(FU_IS_PROGRESS(self), FALSE);
	return self->profile;
}

/**
 * fu_progress_reset:
 * @self: A #FuProgress
 *
 * Resets the #FuProgress object to unset
 *
 * Since: 1.7.0
 **/
void
fu_progress_reset(FuProgress *self)
{
	g_return_if_fail(FU_IS_PROGRESS(self));

	/* reset values */
	self->step_now = 0;
	self->percentage = G_MAXUINT;

	/* only use the timer if profiling; it's expensive */
	if (self->profile) {
		g_timer_start(self->timer);
		g_timer_start(self->timer_child);
	}

	/* no more step data */
	g_ptr_array_set_size(self->children, 0);
}

/**
 * fu_progress_set_steps:
 * @self: A #FuProgress
 * @step_max: The number of sub-tasks in this progress, can be 0
 *
 * Sets the number of sub-tasks, i.e. how many times the fu_progress_step_done()
 * function will be called in the loop.
 *
 * The progress ID must be set fu_progress_set_id() before this method is used.
 *
 * Since: 1.7.0
 **/
void
fu_progress_set_steps(FuProgress *self, guint step_max)
{
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(self->id != NULL);

	/* create fake steps */
	for (guint i = 0; i < step_max; i++)
		fu_progress_add_step(self, self->status, 0, NULL);

	/* show that the sub-progress has been created */
	fu_progress_set_percentage(self, 0);
	fu_progress_add_flag(self, FU_PROGRESS_FLAG_NO_PROFILE);

	/* reset child timer */
	g_timer_start(self->timer_child);
}

/**
 * fu_progress_get_steps:
 * @self: A #FuProgress
 *
 * Gets the number of sub-tasks, i.e. how many times the fu_progress_step_done()
 * function will be called in the loop.
 *
 * Return value: number of sub-tasks in this progress
 *
 * Since: 1.7.0
 **/
guint
fu_progress_get_steps(FuProgress *self)
{
	g_return_val_if_fail(FU_IS_PROGRESS(self), G_MAXUINT);
	return self->children->len;
}

static gdouble
fu_progress_discrete_to_percent(guint discrete, guint step_max)
{
	/* check we are in range */
	if (discrete > step_max)
		return 100;
	if (step_max == 0) {
		g_warning("step_max is 0!");
		return 0;
	}
	return ((gdouble)discrete * (100.0f / (gdouble)(step_max)));
}

static gdouble
fu_progress_get_step_percentage(FuProgress *self, guint idx)
{
	guint current = 0;
	guint total = 0;
	gboolean any_step_weighting = FALSE;

	/* we set the step weighting manually */
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		if (child->step_weighting > 0) {
			any_step_weighting = TRUE;
			break;
		}
	}

	/* just use proportional */
	if (!any_step_weighting)
		return -1;

	/* work out percentage */
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		if (i <= idx)
			current += child->step_weighting;
		total += child->step_weighting;
	}
	return ((gdouble)current * 100.f) / (gdouble)total;
}

static void
fu_progress_child_status_changed_cb(FuProgress *child, FwupdStatus status, FuProgress *self)
{
	fu_progress_set_status(self, status);
}

static void
fu_progress_child_percentage_changed_cb(FuProgress *child, guint percentage, FuProgress *self)
{
	gdouble offset;
	gdouble range;
	gdouble extra;
	guint parent_percentage = G_MAXUINT;

	/* propagate up the stack if FuProgress has only one priv */
	if (self->children->len == 1) {
		fu_progress_set_percentage(self, percentage);
		return;
	}

	/* did we call done on a step that did not have a size set? */
	if (self->children->len == 0)
		return;

	/* already at >= 100% */
	if (self->step_now >= self->children->len) {
		g_warning("already at %u/%u step_max", self->step_now, self->children->len);
		return;
	}

	/* if the child finished, set the status back to the last parent status */
	if (percentage == 100) {
		FuProgress *child_tmp = g_ptr_array_index(self->children, self->step_now);
		if (fu_progress_get_status(child_tmp) != FWUPD_STATUS_UNKNOWN)
			fu_progress_set_status(self, fu_progress_get_status(child_tmp));
	}

	/* we don't store zero */
	if (self->step_now == 0) {
		gdouble pc = fu_progress_get_step_percentage(self, 0);
		if (pc > 0)
			parent_percentage = percentage * pc / 100;
	} else {
		gdouble pc1 = fu_progress_get_step_percentage(self, self->step_now - 1);
		gdouble pc2 = fu_progress_get_step_percentage(self, self->step_now);
		/* bi-linearly interpolate */
		if (pc1 >= 0 && pc2 >= 0)
			parent_percentage = (((100 - percentage) * pc1) + (percentage * pc2)) / 100;
	}
	if (parent_percentage != G_MAXUINT) {
		fu_progress_set_percentage(self, parent_percentage);
		return;
	}

	/* get the range between the parent priv and the next parent priv */
	offset = fu_progress_discrete_to_percent(self->step_now, self->children->len);
	range = fu_progress_discrete_to_percent(self->step_now + 1, self->children->len) - offset;
	if (range < 0.01)
		return;

	/* get the extra contributed by the child */
	extra = ((gdouble)percentage / 100.0f) * range;

	/* emit from the parent */
	parent_percentage = (guint)(offset + extra);
	fu_progress_set_percentage(self, parent_percentage);
}

/**
 * fu_progress_add_step:
 * @self: A #FuProgress
 * @status: status value to use for this phase
 * @value: A step weighting variable argument array
 * @name: (nullable): Human readable name to identify the step
 *
 * This sets the step weighting, which you will want to do if one action
 * will take a bigger chunk of time than another.
 *
 * The progress ID must be set fu_progress_set_id() before this method is used.
 *
 * Since: 1.8.2
 **/
void
fu_progress_add_step(FuProgress *self, FwupdStatus status, guint value, const gchar *name)
{
	g_autoptr(FuProgress) child = fu_progress_new(NULL);

	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(self->id != NULL);

	/* save data */
	fu_progress_set_status(child, status);
	child->step_weighting = value;

	/* connect signals */
	g_signal_connect(FU_PROGRESS(child),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_child_percentage_changed_cb),
			 self);
	g_signal_connect(FU_PROGRESS(child),
			 "status-changed",
			 G_CALLBACK(fu_progress_child_status_changed_cb),
			 self);
	fu_progress_set_parent(child, self);
	if (name != NULL)
		fu_progress_set_name(child, name);

	/* use first child status */
	if (self->children->len == 0)
		fu_progress_set_status(self, status);

	/* add child */
	g_ptr_array_add(self->children, g_steal_pointer(&child));

	/* reset child timer */
	g_timer_start(self->timer_child);
}

/**
 * fu_progress_finished:
 * @self: A #FuProgress
 *
 * Called when the step_now sub-task wants to finish early and still complete.
 *
 * Since: 1.7.0
 **/
void
fu_progress_finished(FuProgress *self)
{
	g_return_if_fail(FU_IS_PROGRESS(self));

	/* is already at 100%? */
	if (self->step_now == self->children->len)
		return;

	/* all done */
	self->step_now = self->children->len;
	fu_progress_set_percentage(self, 100);

	/* we finished early, so invalidate children */
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		fu_progress_add_flag(child, FU_PROGRESS_FLAG_NO_TRACEBACK);
	}
}

/**
 * fu_progress_get_child:
 * @self: A #FuProgress
 *
 * Monitor a child and proxy back up to the parent with the correct percentage.
 *
 * Return value: (transfer none): A new %FuProgress or %NULL for failure
 *
 * Since: 1.7.0
 **/
FuProgress *
fu_progress_get_child(FuProgress *self)
{
	g_return_val_if_fail(FU_IS_PROGRESS(self), NULL);
	g_return_val_if_fail(self->id != NULL, NULL);
	g_return_val_if_fail(self->children->len > 0, NULL);
	g_return_val_if_fail(self->children->len > self->step_now, NULL);

	/* all preallocated, nothing to do */
	return FU_PROGRESS(g_ptr_array_index(self->children, self->step_now));
}

static void
fu_progress_show_profile(FuProgress *self)
{
	gdouble division;
	gdouble total_time = 0.0f;
	gboolean close_enough = TRUE;
	g_autoptr(GString) str = NULL;

	/* not accurate enough for a profile result */
	if (self->flags & FU_PROGRESS_FLAG_NO_PROFILE)
		return;

	/* get the total time so we can work out the divisor */
	str = g_string_new("raw timing data was { ");
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		g_string_append_printf(str, "%.3f, ", fu_progress_get_duration(child));
	}
	if (self->children->len > 0)
		g_string_set_size(str, str->len - 2);
	g_string_append(str, " } -- ");

	/* get the total time so we can work out the divisor */
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		total_time += fu_progress_get_duration(child);
	}
	if (total_time < 0.001)
		return;
	division = total_time / 100.0f;

	/* what we set */
	g_string_append(str, "steps were set as [ ");
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		g_string_append_printf(str, "%u ", child->step_weighting);
	}

	/* what we _should_ have set */
	g_string_append_printf(str, "] but should have been [ ");
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		g_string_append_printf(str, "%.0f ", fu_progress_get_duration(child) / division);

		/* this is sufficiently different to what we guessed */
		if (fabs((fu_progress_get_duration(child) / division) -
			 (gdouble)child->step_weighting) > 5) {
			close_enough = FALSE;
		}
	}
	g_string_append(str, "]");
	if (self->flags & FU_PROGRESS_FLAG_GUESSED) {
#ifdef SUPPORTED_BUILD
		g_debug("%s at %s [%s]", str->str, self->id, fu_progress_get_name_fallback(self));
#else
		g_warning("%s at %s [%s]", str->str, self->id, fu_progress_get_name_fallback(self));
		g_warning("Please see "
			  "https://github.com/fwupd/fwupd/wiki/Daemon-Warning:-FuProgress-steps");
#endif
	} else if (!close_enough) {
		g_debug("%s at %s", str->str, self->id);
	}
}

/**
 * fu_progress_step_done:
 * @self: A #FuProgress
 *
 * Called when the step_now sub-task has finished.
 *
 * Since: 1.7.0
 **/
void
fu_progress_step_done(FuProgress *self)
{
	FuProgress *child = NULL;
	gdouble percentage;

	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(self->id != NULL);

	/* did we call done when no size set? */
	if (self->children->len == 0) {
		g_autoptr(GString) str = g_string_new(NULL);
		fu_progress_build_parent_chain(self, str, 0);
		g_warning("progress done when no size set! [%s]: %s", self->id, str->str);
		return;
	}

	/* get the active child */
	if (self->children->len > 0)
		child = g_ptr_array_index(self->children, self->step_now);

	/* save the duration in the array */
	if (self->profile) {
		if (child != NULL)
			fu_progress_set_duration(child, g_timer_elapsed(self->timer_child, NULL));
		g_timer_start(self->timer_child);
	}

	/* is already at 100%? */
	if (self->step_now >= self->children->len) {
		g_autoptr(GString) str = g_string_new(NULL);
		fu_progress_build_parent_chain(self, str, 0);
		g_warning("already at 100%% [%s]: %s", self->id, str->str);
		return;
	}

	/* is child not at 100%? */
	if (!fu_progress_has_flag(self, FU_PROGRESS_FLAG_CHILD_FINISHED) && child != NULL) {
		if (child->step_now != child->children->len) {
			g_autoptr(GString) str = g_string_new(NULL);
			fu_progress_build_parent_chain(child, str, 0);
			g_warning("child is at %u/%u step_max and parent done [%s]\n%s",
				  child->step_now,
				  child->children->len,
				  self->id,
				  str->str);
			/* do not abort, as we want to clean this up */
		}
	}

	/* another */
	self->step_now++;

	/* update status */
	if (self->step_now < self->children->len) {
		FuProgress *child_tmp = g_ptr_array_index(self->children, self->step_now);
		if (fu_progress_get_status(child_tmp) != FWUPD_STATUS_UNKNOWN)
			fu_progress_set_status(self, fu_progress_get_status(child_tmp));
	} else if (self->parent != NULL) {
		fu_progress_set_status(self, fu_progress_get_status(self->parent));
	} else {
		fu_progress_set_status(self, FWUPD_STATUS_UNKNOWN);
	}

	/* find new percentage */
	percentage = fu_progress_get_step_percentage(self, self->step_now - 1);
	if (percentage < 0)
		percentage = fu_progress_discrete_to_percent(self->step_now, self->children->len);
	fu_progress_set_percentage(self, (guint)percentage);

	/* show any profiling stats */
	if (self->profile && self->step_now == self->children->len)
		fu_progress_show_profile(self);
}

/**
 * fu_progress_sleep:
 * @self: a #FuProgress
 * @delay_ms: the delay in milliseconds
 *
 * Sleeps, setting the device progress from 0..100% as time continues.
 *
 * NOTE: You should try to avoid calling this function for emulated devices.
 *
 * Since: 1.7.0
 **/
void
fu_progress_sleep(FuProgress *self, guint delay_ms)
{
	gulong delay_us_pc = (delay_ms * 1000) / 100;

	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(delay_ms > 0);

	fu_progress_set_percentage(self, 0);
	for (guint i = 0; i < 100; i++) {
		g_usleep(delay_us_pc);
		fu_progress_set_percentage(self, i + 1);
	}
}

static void
fu_progress_traceback_cb(FuProgress *self,
			 guint idt,
			 guint child_idx,
			 guint threshold_ms,
			 GString *str)
{
	if (self->flags & FU_PROGRESS_FLAG_NO_TRACEBACK)
		return;
	if (self->children->len == 0 && fu_progress_get_duration(self) < 0.0001)
		return;
	if (threshold_ms == 0 || fu_progress_get_duration(self) * 1000 > threshold_ms) {
		for (guint i = 0; i < idt; i++)
			g_string_append(str, " ");
		if (self->id != NULL)
			g_string_append(str, self->id);
		if (self->name != NULL)
			g_string_append_printf(str, ":%s", self->name);
		if (self->id == NULL && self->name == NULL && child_idx != G_MAXUINT)
			g_string_append_printf(str, "@%u", child_idx);
		g_string_append_printf(str, " [%.2fms]", fu_progress_get_duration(self) * 1000.f);
		g_string_append(str, self->children->len > 0 ? ":\n" : "\n");
	}
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		fu_progress_traceback_cb(child, idt + 4, i, threshold_ms, str);
	}
}

/**
 * fu_progress_traceback:
 * @self: A #FuProgress
 *
 * Create a traceback used for profiling startup.
 *
 * Return value: (transfer full): string
 *
 * Since: 1.8.2
 **/
gchar *
fu_progress_traceback(FuProgress *self)
{
	const gchar *tmp = g_getenv("FWUPD_PROFILE");
	guint64 threshold_ms = 5000;
	g_autoptr(GString) str = g_string_new(NULL);

	/* allow override */
	if (tmp != NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(tmp, &threshold_ms, 0, G_MAXUINT, &error_local))
			g_warning("invalid threshold value: %s", tmp);
	}

	fu_progress_traceback_cb(self, 0, G_MAXUINT, threshold_ms, str);
	if (str->len == 0)
		return NULL;
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static void
fu_progress_to_string_cb(FuProgress *self, guint idt, GString *str)
{
	/* not interesting */
	if (self->id == NULL && self->name == NULL)
		return;

	if (self->id != NULL)
		fu_string_append(str, idt, "Id", self->id);
	if (self->name != NULL)
		fu_string_append(str, idt, "Name", self->name);
	if (self->percentage != G_MAXUINT)
		fu_string_append_ku(str, idt, "Percentage", self->percentage);
	if (self->status != FWUPD_STATUS_UNKNOWN)
		fu_string_append(str, idt, "Status", fwupd_status_to_string(self->status));
	if (self->duration > 0.0001)
		fu_string_append_ku(str, idt, "DurationMs", self->duration * 1000.f);
	if (self->step_weighting > 0)
		fu_string_append_ku(str, idt, "StepWeighting", self->step_weighting);
	if (self->step_now > 0)
		fu_string_append_ku(str, idt, "StepNow", self->step_now);
	for (guint i = 0; i < self->children->len; i++) {
		FuProgress *child = g_ptr_array_index(self->children, i);
		fu_progress_to_string_cb(child, idt + 1, str);
	}
}

/**
 * fu_progress_to_string:
 * @self: A #FuProgress
 *
 * Prints the progress for debugging.
 *
 * Return value: (transfer full): string
 *
 * Since: 1.8.7
 **/
gchar *
fu_progress_to_string(FuProgress *self)
{
	g_autoptr(GString) str = g_string_new(NULL);
	fu_progress_to_string_cb(self, 0, str);
	if (str->len == 0)
		return NULL;
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static void
fu_progress_init(FuProgress *self)
{
	self->status = FWUPD_STATUS_UNKNOWN;
	self->percentage = G_MAXUINT;
	self->timer = g_timer_new();
	self->timer_child = g_timer_new();
	self->children = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->duration = 0.f;
}

static void
fu_progress_finalize(GObject *object)
{
	FuProgress *self = FU_PROGRESS(object);

	/* show any profiling stats */
	if (self->profile)
		fu_progress_show_profile(self);

	fu_progress_reset(self);
	g_free(self->id);
	g_free(self->name);
	g_ptr_array_unref(self->children);
	g_timer_destroy(self->timer);
	g_timer_destroy(self->timer_child);

	G_OBJECT_CLASS(fu_progress_parent_class)->finalize(object);
}

static void
fu_progress_class_init(FuProgressClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_progress_finalize;

	/**
	 * FuProgress::percentage-changed:
	 * @self: the #FuProgress instance that emitted the signal
	 * @percentage: the new value
	 *
	 * The ::percentage-changed signal is emitted when the tasks completion has changed.
	 *
	 * Since: 1.7.0
	 **/
	signals[SIGNAL_PERCENTAGE_CHANGED] = g_signal_new("percentage-changed",
							  G_TYPE_FROM_CLASS(object_class),
							  G_SIGNAL_RUN_LAST,
							  0,
							  NULL,
							  NULL,
							  g_cclosure_marshal_VOID__UINT,
							  G_TYPE_NONE,
							  1,
							  G_TYPE_UINT);
	/**
	 * FuProgress::status-changed:
	 * @self: the #FuProgress instance that emitted the signal
	 * @status: the new #FwupdStatus
	 *
	 * The ::status-changed signal is emitted when the task status has changed.
	 *
	 * Since: 1.7.0
	 **/
	signals[SIGNAL_STATUS_CHANGED] = g_signal_new("status-changed",
						      G_TYPE_FROM_CLASS(object_class),
						      G_SIGNAL_RUN_LAST,
						      0,
						      NULL,
						      NULL,
						      g_cclosure_marshal_VOID__UINT,
						      G_TYPE_NONE,
						      1,
						      G_TYPE_UINT);
}

/**
 * fu_progress_new:
 * @id: (nullable): progress ID, normally `G_STRLOC`
 *
 * Return value: A new #FuProgress instance.
 *
 * Since: 1.7.0
 **/
FuProgress *
fu_progress_new(const gchar *id)
{
	FuProgress *self;
	self = g_object_new(FU_TYPE_PROGRESS, NULL);
	if (id != NULL)
		fu_progress_set_id(self, id);
	return FU_PROGRESS(self);
}
