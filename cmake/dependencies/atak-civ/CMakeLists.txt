include(FetchContent)

# Schema source only: never configure or build ATAK itself.
FetchContent_Declare(cot_atak_civ
	GIT_REPOSITORY https://github.com/TAK-Product-Center/atak-civ.git
	# Official ATAK-Civ 5.5.1.8 release commit (not the annotated tag).
	GIT_TAG 7d583c8834f7f432da8dfc4a1e7c61d7df65e846
	GIT_SUBMODULES ""
	GIT_SUBMODULES_RECURSE FALSE
	GIT_CONFIG
		filter.lfs.smudge=
		filter.lfs.process=
		filter.lfs.required=false
	SOURCE_SUBDIR cot-schema-only
)
FetchContent_MakeAvailable(cot_atak_civ)

set(cot_proto_dir
	"${cot_atak_civ_SOURCE_DIR}/commoncommo/core/impl/protobuf")
foreach(schema IN ITEMS
	contact cotevent detail group precisionlocation status takcontrol
	takmessage takv track)
	if(NOT EXISTS "${cot_proto_dir}/${schema}.proto" OR
		IS_DIRECTORY "${cot_proto_dir}/${schema}.proto")
		message(FATAL_ERROR "Missing ATAK schema: ${schema}.proto")
	endif()
endforeach()

# Future protobuf generation input and import directory; schemas stay intact.
set(COT_ATAK_PROTO_DIR "${cot_proto_dir}" PARENT_SCOPE)
