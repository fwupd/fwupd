cargs = ['-DG_LOG_DOMAIN="FuPluginSunwinonHid"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('sunwinon-hid.quirk')
plugin_builtins += static_library('fu_plugin_sunwinon_hid',
	rustgen.process(
		'fu-sunwinon-hid.rs',
	),
	sources: [
		'fu-sunwinon-hid-plugin.c',
		'fu-sunwinon-hid-device.c',
		'fu-sunwinon-util-dfu-master.c',
	],
	include_directories: plugin_incdirs,
	link_with: plugin_libs,
	c_args: cargs,
	dependencies: plugin_deps,
)
