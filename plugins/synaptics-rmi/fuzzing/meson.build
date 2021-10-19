install_data([
    'rmi-0x.builder.xml',
    'rmi-10.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-synaptics-rmi-0x',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'rmi-0x.builder.xml'),
     join_paths(meson.current_source_dir(), 'synaptics-rmi-0x.bin'),
  ],
)
run_target('fuzz-targets-synaptics-rmi-10',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'rmi-10.builder.xml'),
     join_paths(meson.current_source_dir(), 'synaptics-rmi-10.bin'),
  ],
)
