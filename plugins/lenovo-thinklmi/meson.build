if not allow_uefi
  subdir_done()
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoThinkLmi"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtin_lenovo_thinklmi = static_library('fu_plugin_lenovo_thinklmi',
  sources: [
    'fu-lenovo-thinklmi-plugin.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_lenovo_thinklmi

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'lenovo-thinklmi-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_lenovo_thinklmi,
      plugin_builtin_uefi_capsule,
    ],
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('lenovo-thinklmi-self-test', e, env: env)
endif
