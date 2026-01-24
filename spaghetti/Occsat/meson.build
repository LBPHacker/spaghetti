ocssat_solvers = [
	# [ internal name, [ possible dependency names, ... ] ],
	[ 'Cryptominisat5', [ 'cryptominisat5' ] ],
]

ocssat_solver_xs = []
occsat_solver_srcs = []
occsat_solver_deps = []
foreach solver : ocssat_solvers
	name = solver[0]
	if get_option('occsat_@0@'.format(name))
		depnames = solver[1]
		ocssat_solver_xs += [ 'X(@0@)'.format(name) ]
		occsat_solver_srcs += [ '@0@.cpp'.format(name) ]
		occsat_solver_deps += [ dependency(depnames) ]
	endif
endforeach

occsat_cdata = configuration_data()
occsat_cdata.set('OccsatSolvers', ' \\\n\t'.join(ocssat_solver_xs))
configure_file(
	input: 'Solvers.template.hpp',
	output: 'Solvers.hpp',
	configuration: occsat_cdata,
)

shared_library(
	'occsat',
	sources: [
		'Solver.cpp',
		'SolverHandle.cpp',
		'ProblemHandle.cpp',
		'LuaInterface.cpp',
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
