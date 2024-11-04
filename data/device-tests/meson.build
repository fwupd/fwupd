install_data([
    'caldigit-element.json',
    'dell-wd19tb.json',
  ],
  install_dir: join_paths(installed_test_datadir, 'device-tests'),
)

con = configuration_data()
enumeration_datadir = join_paths(installed_test_datadir, 'enumeration-data')
con.set('enumeration_datadir', enumeration_datadir)

foreach test: device_tests
  configure_file(
    input: join_paths(meson.project_source_root(),'@0@'.format(test)),
    output: '@BASENAME@.json',
    configuration: con,
    install: true,
    install_dir: join_paths(installed_test_datadir, 'device-tests'),
  )
endforeach

foreach data: enumeration_data
    install_data(data,
      install_dir: enumeration_datadir,
    )
endforeach
