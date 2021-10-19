install_data([
    'acpi-phat.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-acpi-phat',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'acpi-phat.builder.xml'),
     join_paths(meson.current_source_dir(), 'acpi-phat.bin'),
  ],
)
