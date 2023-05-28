if efiboot.found() and efivar.found()
  cargs = ['-DG_LOG_DOMAIN="FuPluginUefiDbx"']

  plugin_quirks += files('uefi-dbx.quirk')
  plugin_builtin_uefi_dbx = static_library(
    'fu_plugin_uefi_dbx',
    rustgen.process('fu-efi-image.rs'),
    sources: [
      'fu-uefi-dbx-plugin.c',
      'fu-uefi-dbx-common.c',
      'fu-uefi-dbx-device.c',
      'fu-efi-image.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
  plugin_builtins += plugin_builtin_uefi_dbx

  if get_option('tests')
    env = environment()
    env.set('G_TEST_SRCDIR', meson.current_source_dir())
    env.set('G_TEST_BUILDDIR', meson.current_build_dir())
    e = executable(
      'uefi-dbx-self-test',
      sources: [
        'fu-self-test.c',
      ],
      include_directories: plugin_incdirs,
      dependencies: plugin_deps,
      link_with: [
        plugin_libs,
        plugin_builtin_uefi_dbx,
      ],
      install: true,
      install_rpath: libdir_pkg,
      install_dir: installed_test_bindir,
      c_args: [
        cargs,
        '-DSRCDIR="' + meson.current_source_dir() + '"',
      ],
    )
    test('uefi-dbx-self-test', e, env: env) # added to installed-tests
  endif

  dbxtool = executable(
    'dbxtool',
    sources: [
      'fu-dbxtool.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_dbx,
    ],
    install: true,
    install_dir: bindir,
    install_rpath: libdir_pkg,
    c_args: cargs,
  )

  if get_option('man')
    custom_target(
      'dbxtool.1',
      input: 'dbxtool.md',
      output: 'dbxtool.1',
      command: [
        generate_man,
        '@INPUT@',
        '-o', '@OUTPUT@',
        '--replace', 'PACKAGE_VERSION',
        fwupd_version,
      ],
      install: true,
      install_dir: join_paths(mandir, 'man1'),
    )
  endif
endif
