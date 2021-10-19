install_data([
    'synaptics-mst.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-synaptics-mst',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'synaptics-mst.builder.xml'),
     join_paths(meson.current_source_dir(), 'synaptics-mst.dat'),
  ],
)
