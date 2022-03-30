/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuProgress"

#include "config.h"

#include <math.h>

#include "fu-progress.h"

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
 * To get a child object, you should use fu_progress_get_child() and then
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

typedef struct {
	gchar *id;
	FuProgressFlags flags;
	guint percentage;
	FwupdStatus status;
	GPtrArray *steps;
	gboolean profile;
	GTimer *timer;
	guint step_now;
	guint step_max;
	gulong percentage_child_id;
	gulong status_child_id;
	FuProgress *child;
	FuProgress *parent; /* no-ref */
} FuProgressPrivate;

typedef struct {
	FwupdStatus status;
	guint value;
	gdouble profile;
} FuProgressStep;

enum { SIGNAL_PERCENTAGE_CHANGED, SIGNAL_STATUS_CHANGED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE_WITH_PRIVATE(FuProgress, fu_progress, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_progress_get_instance_private(o))

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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), NULL);
	return priv->id;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(id != NULL);

	/* not changed */
	if (g_strcmp0(priv->id, id) == 0)
		return;

	/* set id */
	g_free(priv->id);
	priv->id = g_strdup(id);
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), FWUPD_STATUS_UNKNOWN);
	return priv->status;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	priv->flags |= flag;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	priv->flags &= ~flag;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), FALSE);
	return (priv->flags & flag) > 0;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));

	/* not changed */
	if (priv->status == status)
		return;

	/* save */
	priv->status = status;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), G_MAXUINT);
	if (priv->percentage == G_MAXUINT)
		return 0;
	return priv->percentage;
}

static void
fu_progress_build_parent_chain(FuProgress *self, GString *str, guint level)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	if (priv->parent != NULL)
		fu_progress_build_parent_chain(priv->parent, str, level + 1);
	g_string_append_printf(str,
			       "%u) %s (%u/%u)\n",
			       level,
			       priv->id,
			       priv->step_now,
			       priv->step_max);
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(percentage <= 100);

	/* is it the same */
	if (percentage == priv->percentage)
		return;

	/* is it less */
	if (priv->percentage != G_MAXUINT && percentage < priv->percentage) {
		if (priv->profile) {
			g_autoptr(GString) str = g_string_new(NULL);
			fu_progress_build_parent_chain(self, str, 0);
			g_warning("percentage should not go down from %u to %u: %s",
				  priv->percentage,
				  percentage,
				  str->str);
		}
		return;
	}

	/* save */
	priv->percentage = percentage;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	priv->profile = profile;
}

/**
 * fu_progress_get_profile:
 * @self: A #FuProgress
 * @profile:
 *
 * Returns if the profile is enabled for this progress.
 *
 * Return value: if profiling should be enabled
 *
 * Since: 1.7.0
 **/
static gboolean
fu_progress_get_profile(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), FALSE);
	return priv->profile;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));

	/* reset values */
	priv->step_max = 0;
	priv->step_now = 0;
	priv->percentage = G_MAXUINT;

	/* only use the timer if profiling; it's expensive */
	if (priv->profile)
		g_timer_start(priv->timer);

	/* disconnect client */
	if (priv->percentage_child_id != 0) {
		g_signal_handler_disconnect(priv->child, priv->percentage_child_id);
		priv->percentage_child_id = 0;
	}
	if (priv->status_child_id != 0) {
		g_signal_handler_disconnect(priv->child, priv->status_child_id);
		priv->status_child_id = 0;
	}
	g_clear_object(&priv->child);

	/* no more step data */
	g_ptr_array_set_size(priv->steps, 0);
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(priv->id != NULL);

	/* only use the timer if profiling; it's expensive */
	if (priv->profile)
		g_timer_start(priv->timer);

	/* set step_max */
	priv->step_max = step_max;

	/* show that the sub-progress has been created */
	fu_progress_set_percentage(self, 0);
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), G_MAXUINT);
	return priv->step_max;
}

/**
 * fu_progress_add_step:
 * @self: A #FuProgress
 * @status: status value to use for this phase
 * @value: A step weighting variable argument array
 *
 * This sets the step weighting, which you will want to do if one action
 * will take a bigger chunk of time than another.
 *
 * The progress ID must be set fu_progress_set_id() before this method is used.
 *
 * Since: 1.7.0
 **/
void
fu_progress_add_step(FuProgress *self, FwupdStatus status, guint value)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	FuProgressStep *step;

	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(priv->id != NULL);

	/* current status */
	if (priv->steps->len == 0)
		fu_progress_set_status(self, status);

	/* save data */
	step = g_new0(FuProgressStep, 1);
	step->status = status;
	step->value = value;
	step->profile = .0;
	g_ptr_array_add(priv->steps, step);

	/* in case anything is not using ->steps */
	fu_progress_set_steps(self, priv->steps->len);
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
	FuProgressPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(priv->id != NULL);

	/* is already at 100%? */
	if (priv->step_now == priv->step_max)
		return;

	/* all done */
	priv->step_now = priv->step_max;
	fu_progress_set_percentage(self, 100);
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	guint current = 0;
	guint total = 0;

	for (guint i = 0; i < priv->steps->len; i++) {
		FuProgressStep *step = g_ptr_array_index(priv->steps, i);
		if (i <= idx)
			current += step->value;
		total += step->value;
	}
	if (total == 0)
		return 0;
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	gdouble offset;
	gdouble range;
	gdouble extra;
	guint parent_percentage;

	/* propagate up the stack if FuProgress has only one step */
	if (priv->step_max == 1) {
		fu_progress_set_percentage(self, percentage);
		return;
	}

	/* did we call done on a self that did not have a size set? */
	if (priv->step_max == 0)
		return;

	/* already at >= 100% */
	if (priv->step_now >= priv->step_max) {
		g_warning("already at %u/%u step_max", priv->step_now, priv->step_max);
		return;
	}

	/* if the child finished, set the status back to the last parent status */
	if (percentage == 100 && priv->steps->len > 0) {
		FuProgressStep *step = g_ptr_array_index(priv->steps, priv->step_now);
		fu_progress_set_status(self, step->status);
	}

	/* we have to deal with non-linear step_max */
	if (priv->steps->len > 0) {
		/* we don't store zero */
		if (priv->step_now == 0) {
			gdouble pc = fu_progress_get_step_percentage(self, 0);
			parent_percentage = percentage * pc / 100;
		} else {
			gdouble pc1 = fu_progress_get_step_percentage(self, priv->step_now - 1);
			gdouble pc2 = fu_progress_get_step_percentage(self, priv->step_now);
			/* bi-linearly interpolate */
			parent_percentage = (((100 - percentage) * pc1) + (percentage * pc2)) / 100;
		}
		goto out;
	}

	/* get the offset */
	offset = fu_progress_discrete_to_percent(priv->step_now, priv->step_max);

	/* get the range between the parent step and the next parent step */
	range = fu_progress_discrete_to_percent(priv->step_now + 1, priv->step_max) - offset;
	if (range < 0.01)
		return;

	/* get the extra contributed by the child */
	extra = ((gdouble)percentage / 100.0f) * range;

	/* emit from the parent */
	parent_percentage = (guint)(offset + extra);
out:
	fu_progress_set_percentage(self, parent_percentage);
}

static void
fu_progress_set_parent(FuProgress *self, FuProgress *parent)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	priv->parent = parent; /* no ref! */
	priv->profile = fu_progress_get_profile(parent);
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
	FuProgressPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_PROGRESS(self), NULL);
	g_return_val_if_fail(priv->id != NULL, NULL);

	/* already created child */
	if (priv->child != NULL)
		return priv->child;

	/* connect up signals */
	priv->child = fu_progress_new(NULL);
	priv->percentage_child_id =
	    g_signal_connect(FU_PROGRESS(priv->child),
			     "percentage-changed",
			     G_CALLBACK(fu_progress_child_percentage_changed_cb),
			     self);
	priv->status_child_id = g_signal_connect(FU_PROGRESS(priv->child),
						 "status-changed",
						 G_CALLBACK(fu_progress_child_status_changed_cb),
						 self);
	fu_progress_set_parent(priv->child, self);
	return priv->child;
}

static void
fu_progress_show_profile(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	gdouble division;
	gdouble total_time = 0.0f;
	gboolean close_enough = TRUE;
	g_autoptr(GString) str = NULL;

	/* not accurate enough for a profile result */
	if (priv->flags & FU_PROGRESS_FLAG_NO_PROFILE)
		return;

	/* get the total time so we can work out the divisor */
	str = g_string_new("raw timing data was { ");
	for (guint i = 0; i < priv->step_max; i++) {
		FuProgressStep *step = g_ptr_array_index(priv->steps, i);
		g_string_append_printf(str, "%.3f, ", step->profile);
	}
	if (priv->step_max > 0)
		g_string_set_size(str, str->len - 2);
	g_string_append(str, " } -- ");

	/* get the total time so we can work out the divisor */
	for (guint i = 0; i < priv->step_max; i++) {
		FuProgressStep *step = g_ptr_array_index(priv->steps, i);
		total_time += step->profile;
	}
	if (total_time < 0.001)
		return;
	division = total_time / 100.0f;

	/* what we set */
	g_string_append(str, "steps were set as [ ");
	for (guint i = 0; i < priv->step_max; i++) {
		FuProgressStep *step = g_ptr_array_index(priv->steps, i);
		g_string_append_printf(str, "%u ", step->value);
	}

	/* what we _should_ have set */
	g_string_append_printf(str, "] but should have been [ ");
	for (guint i = 0; i < priv->step_max; i++) {
		FuProgressStep *step = g_ptr_array_index(priv->steps, i);
		g_string_append_printf(str, "%.0f ", step->profile / division);

		/* this is sufficiently different to what we guessed */
		if (fabs((gdouble)step->value - step->profile / division) > 5)
			close_enough = FALSE;
	}
	g_string_append(str, "]");
	if (priv->flags & FU_PROGRESS_FLAG_GUESSED) {
#ifdef SUPPORTED_BUILD
		g_debug("%s at %s", str->str, priv->id);
#else
		g_warning("%s at %s", str->str, priv->id);
		g_warning("Please see "
			  "https://github.com/fwupd/fwupd/wiki/Daemon-Warning:-FuProgress-steps");
#endif
	} else if (!close_enough) {
		g_debug("%s at %s", str->str, priv->id);
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
	FuProgressPrivate *priv = GET_PRIVATE(self);
	gdouble percentage;

	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(priv->id != NULL);

	/* did we call done on a self that did not have a size set? */
	if (priv->step_max == 0) {
		g_autoptr(GString) str = g_string_new(NULL);
		fu_progress_build_parent_chain(self, str, 0);
		g_warning("progress done when no size set! [%s]: %s", priv->id, str->str);
		return;
	}

	/* save the duration in the array */
	if (priv->profile) {
		if (priv->steps->len > 0) {
			FuProgressStep *step = g_ptr_array_index(priv->steps, priv->step_now);
			step->profile = g_timer_elapsed(priv->timer, NULL);
		}
		g_timer_start(priv->timer);
	}

	/* is already at 100%? */
	if (priv->step_now >= priv->step_max) {
		g_autoptr(GString) str = g_string_new(NULL);
		fu_progress_build_parent_chain(self, str, 0);
		g_warning("already at 100%% [%s]: %s", priv->id, str->str);
		return;
	}

	/* is child not at 100%? */
	if (priv->child != NULL) {
		FuProgressPrivate *child_priv = GET_PRIVATE(priv->child);
		if (child_priv->step_now != child_priv->step_max) {
			g_autoptr(GString) str = g_string_new(NULL);
			fu_progress_build_parent_chain(priv->child, str, 0);
			g_warning("child is at %u/%u step_max and parent done [%s]\n%s",
				  child_priv->step_now,
				  child_priv->step_max,
				  priv->id,
				  str->str);
			/* do not abort, as we want to clean this up */
		}
	}

	/* another */
	priv->step_now++;

	/* update status */
	if (priv->steps->len > 0) {
		if (priv->step_now == priv->step_max) {
			fu_progress_set_status(self, FWUPD_STATUS_UNKNOWN);
		} else {
			FuProgressStep *step = g_ptr_array_index(priv->steps, priv->step_now);
			fu_progress_set_status(self, step->status);
		}
	}

	/* find new percentage */
	if (priv->steps->len == 0) {
		percentage = fu_progress_discrete_to_percent(priv->step_now, priv->step_max);
	} else {
		percentage = fu_progress_get_step_percentage(self, priv->step_now - 1);
	}
	fu_progress_set_percentage(self, (guint)percentage);

	/* show any profiling stats */
	if (priv->profile && priv->step_now == priv->step_max && priv->steps->len > 0)
		fu_progress_show_profile(self);

	/* reset child if it exists */
	if (priv->child != NULL)
		fu_progress_reset(priv->child);
}

/**
 * fu_progress_sleep:
 * @self: a #FuProgress
 * @delay_ms: the delay in milliseconds
 *
 * Sleeps, setting the device progress from 0..100% as time continues.
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
fu_progress_init(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	priv->percentage = G_MAXUINT;
	priv->timer = g_timer_new();
	priv->steps = g_ptr_array_new_with_free_func(g_free);
}

static void
fu_progress_finalize(GObject *object)
{
	FuProgress *self = FU_PROGRESS(object);
	FuProgressPrivate *priv = GET_PRIVATE(self);

	fu_progress_reset(self);
	g_free(priv->id);
	g_ptr_array_unref(priv->steps);
	g_timer_destroy(priv->timer);

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
	signals[SIGNAL_PERCENTAGE_CHANGED] =
	    g_signal_new("percentage-changed",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FuProgressClass, percentage_changed),
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
	signals[SIGNAL_STATUS_CHANGED] =
	    g_signal_new("status-changed",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FuProgressClass, status_changed),
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
