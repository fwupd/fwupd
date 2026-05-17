cargs = ['-DG_LOG_DOMAIN="FuPluginStarlabsCoreboot"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('starlabs-coreboot.quirk')
plugin_builtin_starlabs_coreboot = static_library('fu_plugin_starlabs_coreboot',
  sources: [
    'fu-starlabs-coreboot-device.c',
    'fu-starlabs-coreboot-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_starlabs_coreboot

if get_option('tests')
  env = environment()
  env.set('G_DEBUG', 'fatal-criticals')
  e = executable(
    'starlabs-coreboot-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_starlabs_coreboot,
    ],
    c_args: [
      cargs,
    ],
  )
  test('starlabs-coreboot-self-test', e, env: env)
endif
