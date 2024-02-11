shared_library(
	'optimize',
	sources: [
		'optimize.cpp',
		'luainterface.cpp',
	],
	dependencies: [
		dependency('lua5.1'),
	],
	name_prefix: '',
	install: true,
	install_dir: install_dir,
	include_directories: include_directories('.'),
)

install_data(
	[
		'bitx.lua',
		'build.lua',
		'check.lua',
		'graph.lua',
		'id_store.lua',
		'init.lua',
		'loader.lua',
		'misc.lua',
		'ordered_map.lua',
		'plot.lua',
		'runner.lua',
		'strict.lua',
		'testbed.lua',
		'user_node.lua',
	],
	install_dir: install_dir,
)
