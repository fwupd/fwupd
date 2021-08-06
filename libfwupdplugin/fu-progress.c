/*
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuProgress"

#include "config.h"

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
 * To get a child object, you should use fu_progress_get_division() and then
 * use the result in any sub-process. You should ensure that the child
 * is not re-used without calling fu_progress_step_done().
 *
 * There are a few nice touches in this module, so that if a module only has
 * one progress step, the child progress is used for updates.
 *
 * <example>
 *   <title>Using a #FuProgress.</title>
 *   <programlisting>
 * static void
 * _do_something(FuProgress *self)
 * {
 *    FuProgress *progress_local;
 *
 *    // setup correct number of steps
 *    fu_progress_set_steps(self, 2);
 *
 *    // run a sub function
 *    progress_local = fu_progress_get_division(self);
 *    _do_something_else1(progress_local);
 *
 *    // this section done
 *    fu_progress_step_done(self);
 *
 *    // run another sub function
 *    progress_local = fu_progress_get_division(self);
 *    _do_something_else2(progress_local);
 *
 *    // this progress done (all complete)
 *    fu_progress_step_done(self);
 * }
 *   </programlisting>
 * </example>
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	gchar *id;
	gboolean enabled;
	guint percentage;

	gboolean profile;
	gdouble global_share;
	gdouble *step_profile;
	GTimer *timer;
	guint step_now;
	guint *step_data;
	guint step_max;
	gulong percentage_child_id;
	FuProgress *child;
	FuProgress *parent; /* no-ref */

} FuProgressPrivate;

enum { SIGNAL_PERCENTAGE_CHANGED, SIGNAL_LAST };

enum { PROP_0, PROP_ID, PROP_LAST };

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
 * Since: 1.6.3
 **/
const gchar *
fu_progress_get_id(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), NULL);
	return priv->id;
}

/**
 * fu_progress_get_enabled:
 * @self: a #FuProgress
 *
 * Get the progress enabled state
 *
 * Returns: %TRUE if the progress is enabled
 *
 * Since: 1.6.3
 **/
gboolean
fu_progress_get_enabled(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), FALSE);
	return priv->enabled;
}

/**
 * fu_progress_set_enabled:
 * @self: a #FuProgress
 * @enabled: enabled state
 *
 * Sets the progress enabled state.
 *
 * Disabling progress tracking is generally a bad thing to do, except when you know you cannot guess
 * the number of steps in the progress.
 *
 * Using this function also reduced the amount of time spent getting a
 * child self using fu_progress_get_division() as a the parent is returned instead.
 *
 * Since: 1.6.3
 **/
void
fu_progress_set_enabled(FuProgress *self, gboolean enabled)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	priv->enabled = enabled;
}

/**
 * fu_progress_get_percentage:
 * @self: a #FuProgress
 *
 * Get the last set progress percentage.
 *
 * Return value: The percentage value, or %G_MAXUINT for error
 *
 * Since: 1.6.3
 **/
guint
fu_progress_get_percentage(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PROGRESS(self), G_MAXUINT);
	return priv->percentage;
}

static void
fu_progress_print_parent_chain(FuProgress *self, guint level)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	if (priv->parent != NULL)
		fu_progress_print_parent_chain(priv->parent, level + 1);
	g_print("%u) %s (%u/%u)\n", level, priv->id, priv->step_now, priv->step_max);
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
 * Since: 1.6.3
 **/
void
fu_progress_set_percentage(FuProgress *self, guint percentage)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(percentage <= 100);

	/* do we care */
	if (!priv->enabled)
		return;

	/* is it the same */
	if (percentage == priv->percentage)
		return;

	/* is it less */
	if (percentage < priv->percentage) {
		if (priv->profile) {
			fu_progress_print_parent_chain(self, 0);
			g_critical("percentage should not go down from %u to %u!",
				   priv->percentage,
				   percentage);
		}
		return;
	}

	/* save */
	priv->percentage = percentage;

	/* are we so low we don't care */
	if (priv->global_share < 0.001)
		return;

	/* emit */
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
 * Since: 1.6.3
 **/
void
fu_progress_set_percentage_full(FuProgress *self, gsize progress_done, gsize progress_total)
{
	gdouble percentage = 0.f;
	g_return_if_fail(FU_IS_PROGRESS(self));
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
 * Since: 1.6.3
 **/
void
fu_progress_set_profile(FuProgress *self, gboolean profile)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));
	priv->profile = profile;
}

/**
 * fu_progress_reset:
 * @self: A #FuProgress
 *
 * Resets the #FuProgress object to unset
 *
 * Since: 1.6.3
 **/
void
fu_progress_reset(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));

	/* do we care */
	if (!priv->enabled)
		return;

	/* reset values */
	priv->step_max = 0;
	priv->step_now = 0;
	priv->percentage = 0;

	/* only use the timer if profiling; it's expensive */
	if (priv->profile)
		g_timer_start(priv->timer);

	/* disconnect client */
	if (priv->percentage_child_id != 0) {
		g_signal_handler_disconnect(priv->child, priv->percentage_child_id);
		priv->percentage_child_id = 0;
	}

	/* unref child */
	g_clear_object(&priv->child);

	/* no more step data */
	g_clear_pointer(&priv->step_data, g_free);
	g_clear_pointer(&priv->step_profile, g_free);
}

/**
 * fu_progress_set_steps_full:
 * @self: A #FuProgress
 * @id: the code location
 * @step_max: The number of sub-tasks in this progress, can be 0
 *
 * Sets the number of sub-tasks, i.e. how many times the fu_progress_step_done()
 * function will be called in the loop.
 *
 * The function will immediately return when the number of step_max is 0
 * or if fu_progress_set_enabled(FALSE) was previously called.
 *
 * Since: 1.6.3
 **/
void
fu_progress_set_steps_full(FuProgress *self, const gchar *id, guint step_max)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));

	/* nothing to do for 0 step_max */
	if (step_max == 0)
		return;

	/* do we care */
	if (!priv->enabled)
		return;

	/* did we call done on a self that did not have a size set? */
	if (priv->step_max != 0) {
		g_warning("step_max already set to %u, can't set %u! [%s]",
			  priv->step_max,
			  step_max,
			  id);
		fu_progress_print_parent_chain(self, 0);
		return;
	}

	/* set id */
	g_free(priv->id);
	priv->id = g_strdup(id);

	/* only use the timer if profiling; it's expensive */
	if (priv->profile)
		g_timer_start(priv->timer);

	/* imply reset */
	fu_progress_reset(self);

	/* set step_max */
	priv->step_max = step_max;

	/* global share just got smaller */
	priv->global_share /= step_max;
}

/**
 * fu_progress_set_custom_steps_full:
 * @self: A #FuProgress
 * @id: the code location
 * @value: A step weighting variable argument array
 *
 * This sets the step weighting, which you will want to do if one action
 * will take a bigger chunk of time than another.
 *
 * All the values must add up to 100, and the list must end with -1.
 * Do not use this function directly, instead use the fu_progress_set_custom_steps() macro.
 *
 * Since: 1.6.3
 **/
void
fu_progress_set_custom_steps_full(FuProgress *self, const gchar *id, guint value, ...)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	va_list args;
	guint total = value;
	guint i;

	g_return_if_fail(FU_IS_PROGRESS(self));

	/* do we care */
	if (!priv->enabled)
		return;

	/* process the valist */
	va_start(args, value);
	for (i = 0;; i++) {
		guint value_temp = va_arg(args, guint);
		if (value_temp == (guint)-1)
			break;
		total += value_temp;
	}
	va_end(args);

	/* does not sum to 100% */
	if (total != 100) {
		g_critical("percentage not 100: %u", total);
		return;
	}

	/* set step number */
	fu_progress_set_steps_full(self, id, i + 1);

	/* save this data */
	total = value;
	priv->step_data = g_new0(guint, i + 2);
	priv->step_profile = g_new0(gdouble, i + 2);
	priv->step_data[0] = total;
	va_start(args, value);
	for (i = 0;; i++) {
		guint value_temp = va_arg(args, guint);
		if (value_temp == (guint)-1)
			break;

		/* we pre-add the data to make access simpler */
		total += value_temp;
		priv->step_data[i + 1] = total;
	}
	va_end(args);
}

/**
 * fu_progress_finished:
 * @self: A #FuProgress
 *
 * Called when the step_now sub-task wants to finish early and still complete.
 *
 * Since: 1.6.3
 **/
void
fu_progress_finished(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PROGRESS(self));

	/* is already at 100%? */
	if (priv->step_now == priv->step_max)
		return;

	/* all done */
	priv->step_now = priv->step_max;

	/* set new percentage */
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
		g_critical("already at %u/%u step_max", priv->step_now, priv->step_max);
		return;
	}

	/* we have to deal with non-linear step_max */
	if (priv->step_data != NULL) {
		/* we don't store zero */
		if (priv->step_now == 0) {
			parent_percentage = percentage * priv->step_data[priv->step_now] / 100;
		} else {
			/* bi-linearly interpolate */
			parent_percentage =
			    (((100 - percentage) * priv->step_data[priv->step_now - 1]) +
			     (percentage * priv->step_data[priv->step_now])) /
			    100;
		}
		goto out;
	}

	/* get the offset */
	offset = fu_progress_discrete_to_percent(priv->step_now, priv->step_max);

	/* get the range between the parent step and the next parent step */
	range = fu_progress_discrete_to_percent(priv->step_now + 1, priv->step_max) - offset;
	if (range < 0.01) {
		g_warning("range=%f (from %u to %u), should be impossible",
			  range,
			  priv->step_now + 1,
			  priv->step_max);
		return;
	}

	/* get the extra contributed by the child */
	extra = ((gdouble)percentage / 100.0f) * range;

	/* emit from the parent */
	parent_percentage = (guint)(offset + extra);
out:
	fu_progress_set_percentage(self, parent_percentage);
}

static void
fu_progress_set_global_share(FuProgress *self, gdouble global_share)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	priv->global_share = global_share;
}

/**
 * fu_progress_get_division:
 * @self: A #FuProgress
 *
 * Monitor a child division and proxy back up to the parent.
 *
 * Return value: (transfer none): A new %FuProgress or %NULL for failure
 *
 * Since: 1.6.3
 **/
FuProgress *
fu_progress_get_division(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	FuProgressPrivate *child_priv;
	FuProgress *child = NULL;

	g_return_val_if_fail(FU_IS_PROGRESS(self), NULL);

	/* do we care */
	if (!priv->enabled)
		return self;

	/* already set child */
	if (priv->child != NULL) {
		g_signal_handler_disconnect(priv->child, priv->percentage_child_id);
		g_object_unref(priv->child);
	}

	/* connect up signals */
	child = fu_progress_new();
	child_priv = GET_PRIVATE(child);
	child_priv->parent = self; /* do not ref! */
	priv->child = child;
	priv->percentage_child_id =
	    g_signal_connect(child,
			     "percentage-changed",
			     G_CALLBACK(fu_progress_child_percentage_changed_cb),
			     self);

	/* set the global share on the new child */
	fu_progress_set_global_share(child, priv->global_share);

	/* set the profile self */
	fu_progress_set_profile(child, priv->profile);
	return child;
}

static void
fu_progress_show_profile(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	gdouble division;
	gdouble total_time = 0.0f;
	GString *result;
	guint i;
	guint uncumalitive = 0;

	/* get the total time so we can work out the divisor */
	result = g_string_new("Raw timing data was { ");
	for (i = 0; i < priv->step_max; i++) {
		g_string_append_printf(result, "%.3f, ", priv->step_profile[i]);
	}
	if (priv->step_max > 0)
		g_string_set_size(result, result->len - 2);
	g_string_append(result, " }\n");

	/* get the total time so we can work out the divisor */
	for (i = 0; i < priv->step_max; i++)
		total_time += priv->step_profile[i];
	division = total_time / 100.0f;

	/* what we set */
	g_string_append(result, "step_max were set as [ ");
	for (i = 0; i < priv->step_max; i++) {
		g_string_append_printf(result, "%u, ", priv->step_data[i] - uncumalitive);
		uncumalitive = priv->step_data[i];
	}

	/* what we _should_ have set */
	g_string_append_printf(result, "-1 ] but should have been: [ ");
	for (i = 0; i < priv->step_max; i++) {
		g_string_append_printf(result, "%.0f, ", priv->step_profile[i] / division);
	}
	g_string_append(result, "-1 ]");
	g_printerr("\n\n%s at %s\n\n", result->str, priv->id);
	g_string_free(result, TRUE);
}

/**
 * fu_progress_step_done:
 * @self: A #FuProgress
 *
 * Called when the step_now sub-task has finished.
 *
 * Since: 1.6.3
 **/
void
fu_progress_step_done(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	gdouble percentage;

	g_return_if_fail(FU_IS_PROGRESS(self));

	/* do we care */
	if (!priv->enabled)
		return;

	/* did we call done on a self that did not have a size set? */
	if (priv->step_max == 0) {
		g_critical("progress done when no size set! [%s]", priv->id);
		fu_progress_print_parent_chain(self, 0);
		return;
	}

	/* save the duration in the array */
	if (priv->profile) {
		gdouble elapsed = g_timer_elapsed(priv->timer, NULL);
		if (priv->step_profile != NULL)
			priv->step_profile[priv->step_now] = elapsed;
		g_timer_start(priv->timer);
	}

	/* is already at 100%? */
	if (priv->step_now >= priv->step_max) {
		g_critical("already at 100%% [%s]", priv->id);
		fu_progress_print_parent_chain(self, 0);
		return;
	}

	/* is child not at 100%? */
	if (priv->child != NULL) {
		FuProgressPrivate *child_priv = GET_PRIVATE(priv->child);
		if (child_priv->step_now != child_priv->step_max) {
			g_print("child is at %u/%u step_max and parent done [%s]\n",
				child_priv->step_now,
				child_priv->step_max,
				priv->id);
			fu_progress_print_parent_chain(priv->child, 0);
			/* do not abort, as we want to clean this up */
		}
	}

	/* another */
	priv->step_now++;

	/* find new percentage */
	if (priv->step_data == NULL) {
		percentage = fu_progress_discrete_to_percent(priv->step_now, priv->step_max);
	} else {
		/* this is cumalative */
		percentage = priv->step_data[priv->step_now - 1];
	}
	fu_progress_set_percentage(self, (guint)percentage);

	/* show any profiling stats */
	if (priv->profile && priv->step_now == priv->step_max && priv->step_profile != NULL)
		fu_progress_show_profile(self);

	/* reset child if it exists */
	if (priv->child != NULL)
		fu_progress_reset(priv->child);
}

/**
 * fu_progress_sleep:
 * @self: a #FuProgress
 * @delay_secs: the delay in seconds
 *
 * Sleeps, setting the device progress from 0..100% as time continues.
 * The value is given in whole seconds as it does not make sense to show the
 * progressbar advancing so quickly for durations of less than one second.
 *
 * Since: 1.6.3
 **/
void
fu_progress_sleep(FuProgress *self, guint delay_secs)
{
	gulong delay_us_pc = (delay_secs * G_USEC_PER_SEC) / 100;

	g_return_if_fail(FU_IS_PROGRESS(self));
	g_return_if_fail(delay_secs > 0);

	fu_progress_set_percentage(self, 0);
	for (guint i = 0; i < 100; i++) {
		g_usleep(delay_us_pc);
		fu_progress_set_percentage(self, i + 1);
	}
}

static void
fu_progress_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuProgress *self = FU_PROGRESS(object);
	FuProgressPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_ID:
		g_value_set_string(value, priv->id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_progress_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuProgress *self = FU_PROGRESS(object);
	FuProgressPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_ID:
		priv->id = g_value_dup_string(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_progress_init(FuProgress *self)
{
	FuProgressPrivate *priv = GET_PRIVATE(self);
	priv->enabled = TRUE;
	priv->global_share = 1.0f;
	priv->timer = g_timer_new();
}

static void
fu_progress_finalize(GObject *object)
{
	FuProgress *self = FU_PROGRESS(object);
	FuProgressPrivate *priv = GET_PRIVATE(self);

	fu_progress_reset(self);
	g_free(priv->id);
	g_free(priv->step_data);
	g_free(priv->step_profile);
	g_timer_destroy(priv->timer);

	G_OBJECT_CLASS(fu_progress_parent_class)->finalize(object);
}

static void
fu_progress_class_init(FuProgressClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_progress_get_property;
	object_class->set_property = fu_progress_set_property;
	object_class->finalize = fu_progress_finalize;

	pspec =
	    g_param_spec_string("id", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_ID, pspec);

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
}

/**
 * fu_progress_new:
 *
 * Return value: A new #FuProgress instance.
 *
 * Since: 1.6.3
 **/
FuProgress *
fu_progress_new(void)
{
	FuProgress *self;
	self = g_object_new(FU_TYPE_PROGRESS, NULL);
	return FU_PROGRESS(self);
}
