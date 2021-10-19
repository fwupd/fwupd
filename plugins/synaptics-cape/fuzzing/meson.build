install_data([
    'synaptics-cape.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-synaptics-cape',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'synaptics-cape.builder.xml'),
     join_paths(meson.current_source_dir(), 'synaptics-cape.fw'),
  ],
)
