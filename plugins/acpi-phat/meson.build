if get_option('plugin_acpi_phat').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginAcpiPhat"']

shared_module('fu_plugin_acpi_phat',
  fu_hash,
  sources : [
    'fu-plugin-acpi-phat.c',
    'fu-acpi-phat.c',                 # fuzzing
    'fu-acpi-phat-health-record.c',   # fuzzing
    'fu-acpi-phat-version-element.c', # fuzzing
    'fu-acpi-phat-version-record.c',  # fuzzing
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'acpi-phat-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-acpi-phat.c',
      'fu-acpi-phat-health-record.c',
      'fu-acpi-phat-version-element.c',
      'fu-acpi-phat-version-record.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    install : true,
    install_dir : installed_test_bindir,
  )
  test('acpi-phat-self-test', e, env : env)  # added to installed-tests
endif
endif
