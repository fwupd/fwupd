cargs = ['-DG_LOG_DOMAIN="FuPluginUefiSspPolicy"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_builtin_uefi_ssp_policy = static_library('fu_plugin_uefi_ssp_policy',
  sources: [
    'fu-uefi-ssp-policy-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_uefi_ssp_policy

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  #env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'uefi-ssp-policy-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_uefi_ssp_policy,
    ],
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('uefi-ssp-policy-self-test', e, env: env)
endif
