// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToBitString)]
enum FuV4lCap {
    None                = 0x00000000,
    VideoCapture        = 0x00000001, // video capture device
    VideoOutput         = 0x00000002, // video output device
    VideoOverlay        = 0x00000004, // video overlay
    VbiCapture          = 0x00000010, // raw VBI capture device
    VbiOutput           = 0x00000020, // raw VBI output device
    SlicedVbiCapture    = 0x00000040, // sliced VBI capture device
    SlicedVbiOutput     = 0x00000080, // sliced VBI output device
    RdsCapture          = 0x00000100, // RDS data capture
    VideoOutputOverlay  = 0x00000200, // video output overlay
    HwFreqSeek          = 0x00000400, // hardware frequency seek
    RdsOutput           = 0x00000800, // RDS encoder
    VideoCaptureMplane  = 0x00001000, // video capture device that supports multiplanar formats
    VideoOutputMplane   = 0x00002000, // video output device that supports multiplanar formats
    VideoM2mMplane      = 0x00004000, // video mem-to-mem device that supports multiplanar formats
    VideoM2m            = 0x00008000, // video mem-to-mem device
    Tuner               = 0x00010000, // has a tuner
    Audio               = 0x00020000, // has audio support
    Radio               = 0x00040000, // is a radio device
    Modulator           = 0x00080000, // has a modulator
    SdrCapture          = 0x00100000, // SDR capture device
    ExtPixFormat        = 0x00200000, // supports the extended pixel format
    SdrOutput           = 0x00400000, // SDR output device
    MetaCapture         = 0x00800000, // metadata capture device
    Readwrite           = 0x01000000, // read/write systemcalls
    Streaming           = 0x04000000, // streaming I/O ioctls
    MetaOutput          = 0x08000000, // metadata output device
    Touch               = 0x10000000, // touch device
    IoMc                = 0x20000000, // input/output controlled by the media controller
    DeviceCaps          = 0x80000000, // sets device capabilities field
}
