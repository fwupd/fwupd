/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use crate::ffi;

glib::wrapper! {
    /// Tracks progress of an operation.
    ///
    /// Wraps the C `FuProgress` type.
    pub struct Progress(Object<ffi::FuProgress>);

    match fn {
        type_ => || ffi::fu_progress_get_type(),
    }
}
