con2 = configuration_data()
con2.set('installedtestsdir',
         join_paths(datadir, 'installed-tests', 'fwupd'))
con2.set('bindir', bindir)

configure_file(
  input : 'fwupdmgr.test.in',
  output : 'fwupdmgr.test',
  configuration : con2,
  install: true,
  install_dir: join_paths('share', 'installed-tests', 'fwupd'),
)

install_data([
    'fwupdmgr.sh',
    'firmware-example.xml.gz',
    'firmware-example.xml.gz.asc',
    'hardware.py',
  ],
  install_dir : 'share/installed-tests/fwupd',
)

gcab = find_program('gcab', required : false)
if gcab.found()
  custom_target('installed-cab123',
    input : [
      'fakedevice123.bin',
      'fakedevice123.bin.asc',
      'fakedevice123.metainfo.xml',
    ],
    output : 'fakedevice123.cab',
    command : [
      gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
    ],
    install: true,
    install_dir: join_paths('share', 'installed-tests', 'fwupd'),
  )
  custom_target('installed-cab124',
    input : [
      'fakedevice124.bin',
      'fakedevice124.bin.asc',
      'fakedevice124.metainfo.xml',
    ],
    output : 'fakedevice124.cab',
    command : [
      gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
    ],
    install: true,
    install_dir: join_paths('share', 'installed-tests', 'fwupd'),
  )
endif
