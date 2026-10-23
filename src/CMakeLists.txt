add_library(cot STATIC cot.cpp allocation.cpp common.cpp protobuf.cpp xml.cpp
	inspect.cpp)
add_library(cot::cot ALIAS cot)
target_include_directories(cot PUBLIC
	$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(cot PRIVATE expat)
target_compile_features(cot PUBLIC c_std_11 cxx_std_17)

if(BUILD_CLI OR (BUILD_TESTING AND BUILD_ACCEPTANCE_TESTS))
	add_library(cot_cli_core STATIC cli.cpp atomic_output.cpp)
	target_link_libraries(cot_cli_core PUBLIC cot::cot)
	target_include_directories(cot_cli_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(BUILD_CLI)
	add_executable(cot_cli cli_main.cpp)
	set_target_properties(cot_cli PROPERTIES OUTPUT_NAME cot)
	target_link_libraries(cot_cli PRIVATE cot_cli_core)
endif()

install(TARGETS cot EXPORT cot-targets
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
if(BUILD_CLI)
	install(TARGETS cot_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
