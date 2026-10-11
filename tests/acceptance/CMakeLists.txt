# The upstream cucumber-cpp library supplies the native wire-server main.
add_executable(acceptance_steps steps.cpp context.cpp details.cpp errors.cpp
	cli.cpp inspection.cpp ../unit/support.cpp ../unit/allocation.cpp ../unit/c_api.c)
target_compile_definitions(acceptance_steps PRIVATE
	COT_RUBY_PATH="${RUBY_EXECUTABLE}"
	COT_PROCESS_HELPER="${CMAKE_CURRENT_SOURCE_DIR}/cli_process.rb")
find_package(Threads REQUIRED)
target_include_directories(acceptance_steps PRIVATE ../unit)
target_link_libraries(acceptance_steps PRIVATE cucumber-cpp GTest::gtest
	cot_cli_core expat Threads::Threads)
if(BUILD_CLI)
	target_compile_definitions(acceptance_steps PRIVATE
		COT_CLI_PATH="$<TARGET_FILE:cot_cli>")
	add_dependencies(acceptance_steps cot_cli)
endif()

add_test(NAME acceptance
	COMMAND "${RUBY_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.rb"
		$<TARGET_FILE:acceptance_steps>
)
set_tests_properties(acceptance PROPERTIES
	LABELS acceptance
	WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
