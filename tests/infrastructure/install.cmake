function(run_checked)
	execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
		OUTPUT_VARIABLE output ERROR_VARIABLE error)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "${ARGV} failed:\n${output}${error}")
	endif()
endfunction()

set(config_args)
if(CONFIGURATION)
	list(APPEND config_args --config "${CONFIGURATION}")
endif()

set(scratch "${BINARY_DIR}/installation-regression")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}/original")
run_checked("${CMAKE_COMMAND}" --install "${BINARY_DIR}"
	${config_args} --prefix "${scratch}/original")
file(RENAME "${scratch}/original" "${scratch}/relocated")
set(prefix "${scratch}/relocated")

foreach(language c cxx)
	set(consumer "${scratch}/${language}")
	run_checked("${CMAKE_COMMAND}"
		-S "${SOURCE_DIR}/tests/infrastructure/install-${language}"
		-B "${consumer}" -G "${GENERATOR}"
		"-DCMAKE_PREFIX_PATH=${prefix}"
		"-DCMAKE_FIND_ROOT_PATH=${prefix}"
		-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
		-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
		-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF)
	run_checked("${CMAKE_COMMAND}" --build "${consumer}"
		${config_args})
	set(executable "${consumer}/consumer${EXECUTABLE_SUFFIX}")
	if(CONFIGURATION AND EXISTS
		"${consumer}/${CONFIGURATION}/consumer${EXECUTABLE_SUFFIX}")
		set(executable
			"${consumer}/${CONFIGURATION}/consumer${EXECUTABLE_SUFFIX}")
	endif()
	run_checked("${executable}")
endforeach()

if(BUILD_CLI)
	run_checked("${prefix}/${INSTALL_BINDIR}/${PACKAGE}${EXECUTABLE_SUFFIX}"
		--help)
endif()
