optimize_inc = include_directories('.')
optimize_sta = static_library(
	'optimizestatic',
	sources: 'optimize.cpp',
	include_directories: optimize_inc,
)
optimize_dep = declare_dependency(
	link_with: optimize_sta,
	include_directories: optimize_inc,
)

shared_library(
	'optimize',
	sources: 'luainterface.cpp',
	dependencies: [
		dependency('lua5.1'),
		optimize_dep,
	],
	name_prefix: '',
	install: true,
	install_dir: install_dir,
)

executable(
	'optimizemain',
	sources: 'main.cpp',
	dependencies: optimize_dep,
)

install_data(
	[
		'bitx.lua',
		'build.lua',
		'check.lua',
		'graph.lua',
		'id_store.lua',
		'init.lua',
		'misc.lua',
		'plot.lua',
		'strict.lua',
		'user_node.lua',
	],
	install_dir: install_dir,
)
