cargs = ['-DG_LOG_DOMAIN="FuPluginUefiDbx"']

shared_module('fu_plugin_uefi_dbx',
  fu_hash,
  sources : [
    'fu-plugin-uefi-dbx.c',
    'fu-uefi-dbx-common.c',
    'fu-uefi-dbx-device.c',
    'fu-efi-image.c',
    'fu-efi-signature.c',
    'fu-efi-signature-common.c',
    'fu-efi-signature-list.c',
    'fu-efi-signature-parser.c',
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
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  e = executable(
    'uefi-dbx-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-uefi-dbx-common.c',
      'fu-efi-image.c',
      'fu-efi-signature.c',
      'fu-efi-signature-common.c',
      'fu-efi-signature-list.c',
      'fu-efi-signature-parser.c',
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
    c_args : cargs,
  )
  test('uefi-dbx-self-test', e)
endif

uefi_dbx_fuzzer = executable(
  'uefi-dbx-fuzzer',
  sources : [
    'fu-fuzzer.c',
    'fu-efi-image.c',
    'fu-efi-signature.c',
    'fu-efi-signature-common.c',
    'fu-efi-signature-list.c',
    'fu-efi-signature-parser.c',
    'fu-uefi-dbx-common.c',
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
)

dbxtool = executable(
  'dbxtool',
  sources : [
    'fu-dbxtool.c',
    'fu-uefi-dbx-common.c',
    'fu-efi-image.c',
    'fu-efi-signature.c',
    'fu-efi-signature-common.c',
    'fu-efi-signature-list.c',
    'fu-efi-signature-parser.c',
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
  c_args : cargs,
)

run_target('fuzz-efidbx',
  command: [
    join_paths(meson.source_root(), 'contrib/afl-fuzz.py'),
    '-i', join_paths(meson.current_source_dir(), 'fuzzing'),
    '-o', join_paths(meson.current_build_dir(), 'findings'),
    '--command', uefi_dbx_fuzzer,
    fwupdtool,
  ],
)
