con2 = configuration_data()
con2.set('installedtestsdir',
         join_paths(get_option('prefix'),
                    get_option('datadir'),
                    'installed-tests', 'fwupd'))
con2.set('bindir',
         join_paths(get_option('prefix'),
                    get_option('bindir')))

testfiles = [
  'fwupdmgr-get-devices.test',
  'fwupdmgr-get-updates.test',
  'fwupdmgr-refresh-offline.test',
  'fwupdmgr-refresh.test',
  'fwupdmgr-verify.test',
]

foreach arg: testfiles
  configure_file(
    input : arg + '.in',
    output : arg,
    configuration : con2,
    install: true,
    install_dir: join_paths('share', 'installed-tests', 'fwupd'),
  )
endforeach

install_data([
    'firmware-example.xml.gz',
    'firmware-example.xml.gz.asc',
  ],
  install_dir : 'share/installed-tests/fwupd',
)
