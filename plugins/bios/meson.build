allow_uefi or subdir_done()
host_cpu in ['x86', 'x86_64'] or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginBios"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtin_bios = static_library('fu_plugin_bios',
  sources: [
    'fu-bios-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_bios

if get_option('tests')
  env = environment()
  env.set('G_DEBUG', 'fatal-criticals')
  e = executable(
    'bios-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_bios,
    ],
    c_args: [
      cargs,
    ],
  )
  test('bios-self-test', e, env: env)
endif
