con2 = configuration_data()
con2.set('installedtestsdir', installed_test_datadir)
con2.set('installedtestsbindir', installed_test_bindir)
con2.set('installedtestsdatadir', installed_test_datadir)
con2.set('bindir', bindir)

configure_file(
  input : 'fwupdmgr.test.in',
  output : 'fwupdmgr.test',
  configuration : con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input : 'fwupdmgr-p2p.test.in',
  output : 'fwupdmgr-p2p.test',
  configuration : con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input : 'fwupd.test.in',
  output : 'fwupd.test',
  configuration : con2,
  install: true,
  install_dir: installed_test_datadir,
)

install_data([
    'fwupdmgr.sh',
    'fwupdmgr-p2p.sh',
    'fwupd-tests.xml',
  ],
  install_dir : installed_test_datadir,
)

install_data([
    'fwupd.sh',
  ],
  install_dir : installed_test_bindir,
)

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
  install_dir: installed_test_datadir,
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
  install_dir: installed_test_datadir,
)

# replace @installedtestsdir@
configure_file(
  input : 'remote.conf.in',
  output : 'fwupd-tests.conf',
  configuration : con2,
  install: true,
  install_dir: join_paths(sysconfdir, 'fwupd', 'remotes.d'),
)
