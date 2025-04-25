shared_library(
	'optimize',
	sources: [
		'optimize.cpp',
		'luainterface.cpp',
	],
	dependencies: [
		dependency('lua5.1'),
	],
	override_options: [
		'cpp_std=c++17',
		'cpp_rtti=false',
		'warning_level=3',
	],
	name_prefix: '',
	install: true,
	install_dir: install_dir,
	include_directories: include_directories('.'),
)
