/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use crate::ffi;
use crate::fwupd_sys;
use glib::translate::*;

glib::wrapper! {
    /// Tracks progress of an operation.
    ///
    /// Wraps the C `FuProgress` type.
    pub struct Progress(Object<ffi::FuProgress>);

    match fn {
        type_ => || ffi::fu_progress_get_type(),
    }
}

impl Progress {
    /// Sets the progress ID (typically `G_STRLOC` in C, use `module_path!()` in Rust).
    #[doc(alias = "fu_progress_set_id")]
    pub fn set_id(&self, id: &str) {
        unsafe {
            ffi::fu_progress_set_id(self.to_glib_none().0, id.to_glib_none().0);
        }
    }

    /// Sets the number of equal-weight steps.
    #[doc(alias = "fu_progress_set_steps")]
    pub fn set_steps(&self, step_max: u32) {
        unsafe {
            ffi::fu_progress_set_steps(self.to_glib_none().0, step_max);
        }
    }

    /// Adds a weighted progress step with a status and optional name.
    #[doc(alias = "fu_progress_add_step")]
    pub fn add_step(&self, status: fwupd_sys::FwupdStatus, value: u32, name: Option<&str>) {
        unsafe {
            ffi::fu_progress_add_step(self.to_glib_none().0, status, value, name.to_glib_none().0);
        }
    }

    /// Marks the current step as done and advances to the next step.
    #[doc(alias = "fu_progress_step_done")]
    pub fn step_done(&self) {
        unsafe {
            ffi::fu_progress_step_done(self.to_glib_none().0);
        }
    }

    /// Gets the child progress for the current step.
    #[doc(alias = "fu_progress_get_child")]
    pub fn child(&self) -> Progress {
        unsafe { from_glib_none(ffi::fu_progress_get_child(self.to_glib_none().0)) }
    }
}
