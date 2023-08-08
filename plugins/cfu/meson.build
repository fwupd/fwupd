if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCfu"']

# do not use structgen as these files are used in the elanfp plugin too...
cfu_rs = custom_target('fu-cfu-rs',
  input: 'fu-cfu.rs',
  output: ['fu-cfu-struct.c', 'fu-cfu-struct.h'],
  command: [
    python3,
    join_paths(meson.project_source_root(), 'libfwupdplugin', 'rustgen.py'),
    '@INPUT@', '@OUTPUT0@', '@OUTPUT1@',
  ],
)

plugin_quirks += files('cfu.quirk')
plugin_builtin_cfu = static_library('fu_plugin_cfu',
  cfu_rs,
  sources: [
    'fu-cfu-device.c',
    'fu-cfu-module.c',
    'fu-cfu-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_cfu
plugincfu_incdir = include_directories('.')
endif
