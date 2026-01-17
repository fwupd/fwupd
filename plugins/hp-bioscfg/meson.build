allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginHpBiosCfg"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtin_hp_bioscfg = static_library('fu_plugin_hp_bioscfg',
  sources: [
    'fu-hp-bioscfg-plugin.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_hp_bioscfg

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'hp-bioscfg-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_hp_bioscfg,
      plugin_builtin_uefi_capsule,
    ],
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('hp-bioscfg-self-test', e, env: env)
endif
