# some of these are controlled by meson tristate features
plugin_deps = [
  gio,
  giounix,
  gnutls,
  gmodule,
  libusb,
  libarchive,
  libjsonglib,
  libxmlb,
  libdrm_amdgpu,
  protobufc,
  fwupdplugin_rs_dep,
]

umockdev_tests = []
umockdev_ioctls = []
device_tests = []
enumeration_data = []
plugin_readme_targets = []
plugin_readme_outputs = []
plugins={}
plugin_paths=[]
subdir('os-indep')
subdir('uefi')
subdir('linux')

if umockdev_integration_tests.allowed()
  envs = environment()
  envs.set('CACHE_DIRECTORY', join_paths(meson.project_build_root(), 'cache'))
  envs.set('DAEMON_BUILDDIR', join_paths(meson.project_build_root(), 'src'))
  envs.set('FWUPD_DATADIR_QUIRKS', join_paths(meson.project_build_root()))
  envs.set('GI_TYPELIB_PATH', join_paths(meson.project_build_root(), 'libfwupd'))
  envs.set('LD_LIBRARY_PATH', join_paths(meson.project_build_root(), 'libfwupd'))
  envs.set('PYTHONPATH', join_paths(meson.project_source_root(), 'data', 'tests'))
  envs.set('STATE_DIRECTORY', join_paths(meson.project_build_root(), 'state'))

  foreach suite: umockdev_tests
    r = run_command(unittest_inspector, suite,
                    check: true, env: envs)
    unit_tests = r.stdout().strip().split('\n')
    foreach ut: unit_tests
        test(ut, python3, args: [suite, ut], is_parallel: false, env: envs)
    endforeach
    install_data(suite,
      install_dir: installed_test_datadir,
    )
  endforeach
  foreach ioctl: umockdev_ioctls
    install_data(ioctl,
      install_dir: installed_test_datadir,
    )
  endforeach
endif
