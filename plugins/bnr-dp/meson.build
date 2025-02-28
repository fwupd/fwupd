cargs = ['-DG_LOG_DOMAIN="FuPluginBnrDp"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('bnr-dp.quirk')
plugin_rustgen_output = rustgen.process('fu-bnr-dp.rs')
plugin_builtin_bnr_dp = static_library(
    'fu_plugin_bnr_dp',
    plugin_rustgen_output,
    sources: [
        'fu-bnr-dp-common.c',
        'fu-bnr-dp-device.c',
        'fu-bnr-dp-firmware.c',
        'fu-bnr-dp-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_bnr_dp

if get_option('tests')
    install_data(
        ['tests/bnr-dp.builder.xml'],
        install_dir: join_paths(installed_test_datadir, 'tests'),
    )
    env = environment()
    env.set('G_TEST_SRCDIR', meson.current_source_dir())
    env.set('G_TEST_BUILDDIR', meson.current_build_dir())
    e = executable(
        'bnr-self-test',
        plugin_rustgen_output,
        sources: [
            'fu-self-test.c',
        ],
        include_directories: plugin_incdirs,
        dependencies: plugin_deps,
        link_with: [
            plugin_libs,
            plugin_builtin_bnr_dp,
        ],
        install: true,
        install_rpath: libdir_pkg,
        install_dir: installed_test_bindir,
        c_args: [
            '-DSRCDIR="' + meson.current_source_dir() + '"',
        ],
    )
    test('bnr-dp-self-test', e, env: env)
endif
