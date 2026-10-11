add_executable(unit_tests main.cpp)
target_sources(unit_tests PRIVATE
	support.cpp allocation.cpp arguments.cpp ownership.cpp events.cpp
	details.cpp c_api.c parser.cpp inspection.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(unit_tests PRIVATE cot::cot GTest::gtest Threads::Threads)

# The test allocation translation unit supplies the private allocation seam.
# The static archive's allocation.cpp member is then not extracted; the same
# codec object is tested, without runtime hooks or a second compiled copy.
target_include_directories(unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")

include(register.cmake)
