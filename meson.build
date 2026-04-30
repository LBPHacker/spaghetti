project('spaghetti', 'cpp', meson_version: '>=1.1')

project_options = [
	'cpp_std=c++17',
	'cpp_rtti=false',
	'warning_level=3',
]
lua51_dep = dependency('lua5.1')

install_dir = get_option('install_dir')
module_install_dir = join_paths(install_dir, 'spaghetti')
subdir('spaghetti')

install_data(
	[
		'spaghetti.build.modulepack',
		'spaghetti.plot.modulepack',
	],
	install_dir: install_dir,
)
