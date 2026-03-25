/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use crate::ffi;

glib::wrapper! {
    /// A context shared between the daemon and plugins.
    ///
    /// Wraps the C `FuContext` type.
    pub struct Context(Object<ffi::FuContext, ffi::FuContextClass>);

    match fn {
        type_ => || ffi::fu_context_get_type(),
    }
}
