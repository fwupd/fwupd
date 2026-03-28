/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Chunk and ChunkArray types for splitting firmware into transfer blocks.

use crate::ffi;
use glib::translate::*;

glib::wrapper! {
    /// An array of firmware chunks, typically used for splitting firmware
    /// images into device-sized transfer blocks.
    pub struct ChunkArray(Object<ffi::FuChunkArray>);

    match fn {
        type_ => || ffi::fu_chunk_array_get_type(),
    }
}

impl ChunkArray {
    /// Creates a new chunk array from an input stream.
    ///
    /// Splits the stream into chunks of `packet_sz` bytes starting at
    /// `addr_offset`. Use `page_sz = 0` for no page boundaries.
    #[doc(alias = "fu_chunk_array_new_from_stream")]
    pub fn from_stream(
        stream: &gio::InputStream,
        addr_offset: usize,
        page_sz: usize,
        packet_sz: usize,
    ) -> Result<ChunkArray, glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let stream_ptr: *mut gio::ffi::GInputStream = stream.to_glib_none().0;
            let ret = ffi::fu_chunk_array_new_from_stream(
                stream_ptr as *mut _,
                addr_offset,
                page_sz,
                packet_sz,
                &mut error,
            );
            if error.is_null() {
                Ok(from_glib_full(ret))
            } else {
                Err(from_glib_full(error))
            }
        }
    }

    /// Returns the number of chunks.
    #[doc(alias = "fu_chunk_array_length")]
    pub fn len(&self) -> u32 {
        unsafe { ffi::fu_chunk_array_length(self.to_glib_none().0) }
    }

    /// Returns whether the array is empty.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Gets a chunk by index.
    #[doc(alias = "fu_chunk_array_index")]
    pub fn index(&self, idx: u32) -> Result<Chunk, glib::Error> {
        unsafe {
            let mut error = std::ptr::null_mut();
            let ret = ffi::fu_chunk_array_index(self.to_glib_none().0, idx, &mut error);
            if error.is_null() {
                Ok(from_glib_full(ret))
            } else {
                Err(from_glib_full(error))
            }
        }
    }
}

glib::wrapper! {
    /// A single chunk of firmware data with an address and index.
    pub struct Chunk(Object<ffi::FuChunk>);

    match fn {
        type_ => || ffi::fu_chunk_get_type(),
    }
}

impl Chunk {
    /// Gets the start address of this chunk.
    #[doc(alias = "fu_chunk_get_address")]
    pub fn address(&self) -> usize {
        unsafe { ffi::fu_chunk_get_address(self.to_glib_none().0) }
    }

    /// Gets the data size in bytes.
    #[doc(alias = "fu_chunk_get_data_sz")]
    pub fn data_sz(&self) -> usize {
        unsafe { ffi::fu_chunk_get_data_sz(self.to_glib_none().0) }
    }

    /// Gets the chunk data as a byte slice.
    #[doc(alias = "fu_chunk_get_data")]
    pub fn data(&self) -> &[u8] {
        unsafe {
            let ptr = ffi::fu_chunk_get_data(self.to_glib_none().0);
            let len = ffi::fu_chunk_get_data_sz(self.to_glib_none().0);
            if ptr.is_null() || len == 0 {
                &[]
            } else {
                std::slice::from_raw_parts(ptr, len)
            }
        }
    }

    /// Gets the chunk index.
    #[doc(alias = "fu_chunk_get_idx")]
    pub fn idx(&self) -> u32 {
        unsafe { ffi::fu_chunk_get_idx(self.to_glib_none().0) }
    }
}
