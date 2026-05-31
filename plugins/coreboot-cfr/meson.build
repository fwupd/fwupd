host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginCorebootCfr"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtin_coreboot_cfr = static_library('fu_plugin_coreboot_cfr',
  sources: [
    'fu-coreboot-cfr-plugin.c',
    'fu-coreboot-cfr-setting.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_coreboot_cfr

if get_option('tests')
  env = environment()
  env.set('G_DEBUG', 'fatal-criticals')
  e = executable(
    'coreboot-cfr-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_coreboot_cfr,
    ],
    c_args: [
      cargs,
    ],
  )
  test('coreboot-cfr-self-test', e, env: env)
endif
