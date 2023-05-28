#!/bin/sh
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
# SPDX-License-Identifier: LGPL-2.1+
find . -name meson.build -exec muon fmt -c ./contrib/muon_fmt.ini -i {} +
