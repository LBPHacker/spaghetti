project('spaghetti', 'cpp', meson_version: '>=1.1')

project_options = [
	'cpp_std=c++17',
	'cpp_rtti=false',
	'warning_level=3',
]
lua51_dep = dependency('lua5.1')

install_dir = join_paths(get_option('install_dir'), 'spaghetti')
subdir('spaghetti')
