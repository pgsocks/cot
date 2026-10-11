include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")
CPMAddPackage(
	NAME Expat
	GITHUB_REPOSITORY libexpat/libexpat
	# Expat 2.9.0 release commit.
	GIT_TAG a76b1174c1ea52abb639a4c69d52b630a8a106a2
	SOURCE_SUBDIR expat
	EXCLUDE_FROM_ALL YES
	OPTIONS
		"EXPAT_BUILD_TOOLS OFF"
		"EXPAT_BUILD_EXAMPLES OFF"
		"EXPAT_BUILD_TESTS OFF"
		"EXPAT_BUILD_DOCS OFF"
		"EXPAT_BUILD_PKGCONFIG OFF"
		"EXPAT_SHARED_LIBS OFF"
		"EXPAT_ENABLE_INSTALL OFF"
)

# Expat's getenv calls enable process-wide diagnostic output. Keep its private
# parser build independent of that environment to honor the codec's no-I/O
# contract. This does not change the application's getenv or upstream files.
if(MSVC)
	set_property(SOURCE "${Expat_SOURCE_DIR}/expat/lib/xmlparse.c"
		TARGET_DIRECTORY expat APPEND PROPERTY COMPILE_OPTIONS
		"/FI${CMAKE_CURRENT_SOURCE_DIR}/environment.h")
else()
	set_property(SOURCE "${Expat_SOURCE_DIR}/expat/lib/xmlparse.c"
		TARGET_DIRECTORY expat APPEND PROPERTY COMPILE_OPTIONS
		-include "${CMAKE_CURRENT_SOURCE_DIR}/environment.h")
endif()
