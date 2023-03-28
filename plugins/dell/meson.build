tpm2tss_dell = dependency('tss2-esys', version: '>= 2.0', required: get_option('plugin_dell'))
libsmbios_c = dependency('libsmbios_c', version: '>= 2.4.0', required: get_option('plugin_dell'))

if tpm2tss_dell.found() and libsmbios_c.found() and \
   get_option('plugin_dell').require(get_option('plugin_uefi_capsule').allowed(),
    error_message: 'plugin_uefi_capsule is needed for plugin_dell').allowed()

cargs = ['-DG_LOG_DOMAIN="FuPluginDell"']

plugin_quirks += files('dell.quirk')

plugin_builtin_dell = static_library('fu_plugin_dell',
  sources: [
    'fu-dell-plugin.c',
    'fu-dell-smi.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: [
    cargs,
  ],
  dependencies: [
    plugin_deps,
    libsmbios_c,
    tpm2tss_dell,
  ],
)
plugin_builtins += plugin_builtin_dell

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'dell-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libsmbios_c,
      valgrind,
      tpm2tss_dell,
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_capsule,
      plugin_builtin_dell,
    ],
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('dell-self-test', e, env: env)
endif
endif
