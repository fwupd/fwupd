
uefi_plugins = {
  'uefi-capsule': false,
  'uefi-db': false,
  'uefi-dbx': false,
  'uefi-esrt': false,
  'uefi-kek': false,
  'uefi-mok': false,
  'uefi-pk': false,
  'uefi-recovery': false,
  'uefi-sbat': false,
}

plugins = plugins + uefi_plugins

if not allow_uefi
  subdir_done()
endif

foreach plugin, enabled: uefi_plugins
  subdir(plugin)
  plugin_readme_output = '@0@-README.md'.format(plugin)
  plugin_readme_targets += custom_target('doc-plugin-@0@'.format(plugin),
    input: join_paths(meson.project_source_root(), 'plugins', 'uefi', plugin, 'README.md'),
    output: plugin_readme_output,
    command: ['ln', '-sf', '@INPUT0@', '@OUTPUT@'],
    build_by_default: true,
  )
  plugin_readme_outputs += '"@0@"'.format(plugin_readme_output)
endforeach
