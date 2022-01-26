#!/bin/sh
meson build && ninja -C build test
