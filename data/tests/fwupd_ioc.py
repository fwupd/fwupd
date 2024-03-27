# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2013-2018 Vincent Pelletier <plr.vincent@gmail.com>
"""
Pythonified linux asm-generic/ioctl.h .

Produce IOCTL command numbers from their individual components, simplifying
C header conversion to python (keeping magic constants and differences to
C code to a minimum).

Common parameter meanings:
    type (8-bits unsigned integer)
        Driver-imposed ioctl number.
    nr (8-bits unsigned integer)
        Driver-imposed ioctl function number.
"""
import array
import ctypes
import struct

_IOC_NRBITS = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_DIRBITS = 2

_IOC_NRMASK = (1 << _IOC_NRBITS) - 1
_IOC_TYPEMASK = (1 << _IOC_TYPEBITS) - 1
_IOC_SIZEMASK = (1 << _IOC_SIZEBITS) - 1
_IOC_DIRMASK = (1 << _IOC_DIRBITS) - 1

_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS

IOC_NONE = 0
IOC_WRITE = 1
IOC_READ = 2


def IOC(dir, type, nr, size):
    """
    dir
        One of IOC_NONE, IOC_WRITE, IOC_READ, or IOC_READ|IOC_WRITE.
        Direction is from the application's point of view, not kernel's.
    size (14-bits unsigned integer)
        Size of the buffer passed to ioctl's "arg" argument.
    """
    assert dir <= _IOC_DIRMASK, dir
    assert type <= _IOC_TYPEMASK, type
    assert nr <= _IOC_NRMASK, nr
    assert size <= _IOC_SIZEMASK, size
    return (
        (dir << _IOC_DIRSHIFT)
        | (type << _IOC_TYPESHIFT)
        | (nr << _IOC_NRSHIFT)
        | (size << _IOC_SIZESHIFT)
    )


def IOC_TYPECHECK(t):
    """
    Returns the size of given type, and check its suitability for use in an
    ioctl command number.
    """
    if isinstance(t, (memoryview, bytearray)):
        size = len(t)
    elif isinstance(t, struct.Struct):
        size = t.size
    elif isinstance(t, array.array):
        size = t.itemsize * len(t)
    else:
        size = ctypes.sizeof(t)
    assert size <= _IOC_SIZEMASK, size
    return size


def IO(type, nr):
    """
    An ioctl with no parameters.
    """
    return IOC(IOC_NONE, type, nr, 0)


def IOR(type, nr, size):
    """
    An ioctl with read parameters.

    size (ctype type or instance, memoryview, bytearray, struct.Struct, or array.array)
        Type/structure of the argument passed to ioctl's "arg" argument.
    """
    return IOC(IOC_READ, type, nr, IOC_TYPECHECK(size))


def IOW(type, nr, size):
    """
    An ioctl with write parameters.

    size (ctype type or instance, memoryview, bytearray, struct.Struct, or array.array)
        Type/structure of the argument passed to ioctl's "arg" argument.
    """
    return IOC(IOC_WRITE, type, nr, IOC_TYPECHECK(size))


def IOWR(type, nr, size):
    """
    An ioctl with both read an writes parameters.

    size (ctype type or instance, memoryview, bytearray, struct.Struct, or array.array)
        Type/structure of the argument passed to ioctl's "arg" argument.
    """
    return IOC(IOC_READ | IOC_WRITE, type, nr, IOC_TYPECHECK(size))


def IOC_DIR(nr):
    """
    Extract direction from an ioctl command number.
    """
    return (nr >> _IOC_DIRSHIFT) & _IOC_DIRMASK


def IOC_TYPE(nr):
    """
    Extract type from an ioctl command number.
    """
    return (nr >> _IOC_TYPESHIFT) & _IOC_TYPEMASK


def IOC_NR(nr):
    """
    Extract nr from an ioctl command number.
    """
    return (nr >> _IOC_NRSHIFT) & _IOC_NRMASK


def IOC_SIZE(nr):
    """
    Extract size from an ioctl command number.
    """
    return (nr >> _IOC_SIZESHIFT) & _IOC_SIZEMASK


IOC_IN = IOC_WRITE << _IOC_DIRSHIFT
IOC_OUT = IOC_READ << _IOC_DIRSHIFT
IOC_INOUT = (IOC_WRITE | IOC_READ) << _IOC_DIRSHIFT
IOCSIZE_MASK = _IOC_SIZEMASK << _IOC_SIZESHIFT
IOCSIZE_SHIFT = _IOC_SIZESHIFT
