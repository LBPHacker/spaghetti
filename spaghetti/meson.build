project_options = [
	'cpp_std=c++17',
	'cpp_rtti=false',
	'warning_level=3',
]
lua51_dep = dependency('lua5.1')

shared_library(
	'optimize',
	sources: [
		'optimize.cpp',
	],
	dependencies: [
		lua51_dep,
	],
	override_options: project_options,
	name_prefix: '',
	install: true,
	install_dir: install_dir,
	include_directories: include_directories('.'),
)

ocssat_solvers = [
	# [ internal name, [ possible dependency names, ... ] ],
	[ 'cryptominisat5', [ 'cryptominisat5' ] ],
]

ocssat_solver_xs = []
occsat_solver_srcs = []
occsat_solver_deps = []
foreach solver : ocssat_solvers
	name = solver[0]
	if get_option('occsat_@0@'.format(name))
		depnames = solver[1]
		ocssat_solver_xs += [ 'X(@0@)'.format(name) ]
		occsat_solver_srcs += [ 'occsat_@0@.cpp'.format(name) ]
		occsat_solver_deps += [ dependency(depnames) ]
	endif
endforeach

occsat_cdata = configuration_data()
occsat_cdata.set('OCSSAT_SOLVERS', ' \\\n\t'.join(ocssat_solver_xs))
configure_file(
	input: 'occsat.template.hpp',
	output: 'occsat.hpp',
	configuration: occsat_cdata,
)

shared_library(
	'occsat',
	sources: [
		'occsat.cpp',
		occsat_solver_srcs,
	],
	dependencies: [
		lua51_dep,
		occsat_solver_deps,
	],
	override_options: project_options,
	name_prefix: '',
	install: true,
	install_dir: install_dir,
	include_directories: include_directories('.'),
)
