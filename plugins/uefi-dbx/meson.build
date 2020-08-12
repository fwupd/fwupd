cargs = ['-DG_LOG_DOMAIN="FuPluginUefiDbx"']

uefi_dbx_fuzzer = executable(
  'uefi-dbx-fuzzer',
  sources : [
    'fu-fuzzer.c',
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
)

dbxtool = executable(
  'dbxtool',
  sources : [
    'fu-dbxtool.c',
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
