con2 = configuration_data()
con2.set('installedtestsdir', installed_test_datadir)
con2.set('installedtestsbindir', installed_test_bindir)
con2.set('installedtestsdatadir', installed_test_datadir)
con2.set('devicetestdir', join_paths(installed_test_datadir, 'device-tests'))
con2.set('bindir', bindir)
con2.set('libexecdir', libexecdir)

configure_file(
  input: 'fwupdmgr.test.in',
  output: 'fwupdmgr.test',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupdmgr-p2p.test.in',
  output: 'fwupdmgr-p2p.test',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupdtool.test.in',
  output: 'fwupdtool.test',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupdtool-efiboot.test.in',
  output: 'fwupdtool-efiboot.test',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupd.test.in',
  output: 'fwupd.test',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupdmgr-p2p.sh',
  output: 'fwupdmgr-p2p.sh',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

install_data([
    'build-certs.py',
    'fakedevice124.bin',
    'fakedevice124.jcat',
    'fakedevice124.metainfo.xml',
    'fwupd-tests.xml',
    'lsan-suppressions.txt',
  ],
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupdmgr.sh',
  output: 'fwupdmgr.sh',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupdtool.sh',
  output: 'fwupdtool.sh',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupdtool-efiboot.sh',
  output: 'fwupdtool-efiboot.sh',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

configure_file(
  input: 'fwupd.sh',
  output: 'fwupd.sh',
  configuration: con2,
  install: true,
  install_dir: installed_test_datadir,
)

custom_target('installed-cab123',
  input: [
    'fakedevice123.bin',
    'fakedevice123.jcat',
    'fakedevice123.metainfo.xml',
  ],
  output: 'fakedevice123.cab',
  command: [
    gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
  ],
  install: true,
  install_dir: installed_test_datadir,
)
custom_target('installed-cab124',
  input: [
    'fakedevice124.bin',
    'fakedevice124.jcat',
    'fakedevice124.metainfo.xml',
  ],
  output: 'fakedevice124.cab',
  command: [
    gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
  ],
  install: true,
  install_dir: installed_test_datadir,
)

# replace @installedtestsdir@
configure_file(
  input: 'remote.conf.in',
  output: 'fwupd-tests.conf',
  configuration: con2,
  install: true,
  install_dir: join_paths(datadir, 'fwupd', 'remotes.d'),
)

if umockdev_integration_tests.allowed()
  fwupd_mockdev_tests = files('fwupd_test.py')
  r = run_command(unittest_inspector, fwupd_mockdev_tests, check: true)
  unit_tests = r.stdout().strip().split('\n')
  foreach ut: unit_tests
      test(ut, python3, args: [fwupd_mockdev_tests, ut], is_parallel: false,
          env: {
            'DAEMON_BUILDDIR': join_paths(meson.project_build_root(), 'src'),
            'LIBFWUPD_BUILD_DIR': join_paths(meson.project_build_root(), 'libfwupd'),
            'STATE_DIRECTORY': join_paths(meson.project_build_root(), 'state'),
            'CACHE_DIRECTORY': join_paths(meson.project_build_root(), 'cache'),
          },
          )
  endforeach
  configure_file(
    input: fwupd_mockdev_tests,
    output: 'fwupd_test.py',
    configuration: {
      'LIBEXECDIR': daemon_dir,
    },
    install_dir: installed_test_datadir
  )
  install_data('fwupd_ioc.py', install_dir: installed_test_datadir)
endif
