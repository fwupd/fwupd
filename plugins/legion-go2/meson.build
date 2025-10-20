# Copyright 2025 lazro <2059899519@qq.com>
# SPDX-License-Identifier: LGPL-2.1-or-later

cargs = ['-DG_LOG_DOMAIN="FuPluginLegionGo2"']

plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('legion-go2.quirk')

plugin_builtins += static_library('fu_plugin_legion_go2',
  rustgen.process('fu-legion-go2.rs'),
  sources: [
    'fu-legion-go2-plugin.c',
    'fu-legion-go2-device.c',
    'fu-legion-go2-firmware.c'
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
