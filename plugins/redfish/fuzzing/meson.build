install_data([
    'redfish-smbios.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-redfish-smbios',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'redfish-smbios.builder.xml'),
     join_paths(meson.current_source_dir(), 'redfish-smbios.bin'),
  ],
)
