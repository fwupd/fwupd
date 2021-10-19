install_data([
    'synaprom.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-synaptics-prometheus',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'synaprom.builder.xml'),
     join_paths(meson.current_source_dir(), 'synaprom.bin'),
  ],
)
