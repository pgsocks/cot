cmake_minimum_required(VERSION 3.24)
project(native_repo_template LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
option(BUILD_UNIT_TESTS "Build GoogleTest unit tests" ON)
option(BUILD_ACCEPTANCE_TESTS "Build Cucumber acceptance tests" OFF)
option(BUILD_CLI "Build the cot command-line converter" ON)

option(COT_VERIFY_UPSTREAM_SCHEMA
	"Fetch and verify the pinned upstream Protobuf schema source" OFF)
if(COT_VERIFY_UPSTREAM_SCHEMA)
	add_subdirectory(cmake/dependencies/atak-civ EXCLUDE_FROM_ALL)
endif()
add_subdirectory(cmake/dependencies/expat)

add_subdirectory(src)

if(BUILD_TESTING AND (BUILD_UNIT_TESTS OR BUILD_ACCEPTANCE_TESTS))
	if(BUILD_ACCEPTANCE_TESTS)
		include(cmake/AcceptancePrerequisites.cmake)
	endif()
	add_subdirectory(cmake/dependencies EXCLUDE_FROM_ALL)
	add_subdirectory(tests)
endif()

configure_package_config_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/cot-config.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/cot-config.cmake"
	INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cot"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cot-config.cmake"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cot")
install(EXPORT cot-targets NAMESPACE cot::
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cot")
install(DIRECTORY include/cot
	DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
