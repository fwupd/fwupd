host_machine.system() == 'linux' or subdir_done()
host_cpu in ['x86', 'x86_64'] or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginThunderbolt"']

plugin_quirks += files('thunderbolt.quirk')
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtin_thunderbolt = static_library('fu_plugin_thunderbolt',
  rustgen.process('fu-thunderbolt.rs'),
  sources: [
    'fu-thunderbolt-plugin.c',
    'fu-thunderbolt-common.c',
    'fu-thunderbolt-device.c',
    'fu-thunderbolt-retimer.c',
    'fu-thunderbolt-controller.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_thunderbolt

device_tests += files(
  'tests/caldigit-ts4-tbt.json',
  'tests/lenovo-x280-tbt.json',
)
