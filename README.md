# CoT

CoT is a native library project for converting Cursor-on-Target events between
XML and the official Team Awareness Kit (TAK) Protobuf representation. CoT
messages describe events with geospatial coordinates. The project uses the
[official ATAK-Civ repository][atak] as its Protobuf schema authority and defines
matching C11 and procedural C++17 serialization APIs.

## Usage

The public interfaces are declared in [cot.h](include/cot/cot.h) and
[cot.hpp](include/cot/cot.hpp). They use plain event structs, counted UTF-8
text, UTC epoch-millisecond timestamps, and explicit ownership. Generated
Protobuf types remain private. See the [API contract](docs/api.md) for C/C++
construction, parsing, conversion, and cleanup examples.

The `cot` executable converts one complete message per invocation:

```text
cot encode [INPUT|-] [-o OUTPUT|-]   # XML -> Protobuf
cot decode [INPUT|-] [-o OUTPUT|-]   # Protobuf -> XML
```

Omitted paths and `-` select stdin/stdout. Protobuf means an unframed official
`TakMessage` containing a CoT event without control data. Shell workflows:

```sh
cat event.xml | cot encode -o event.pb
cot decode event.pb > decoded.xml
cot decode < event.pb | cot encode > roundtrip.pb
```

See the [CLI contract](docs/cli.md) for options, exit codes, binary I/O, and
destination-file guarantees.

## Configure, build, and test

Install CMake 3.24 or newer, Git, and a C/C++ toolchain supporting C11 and
C++17. Initial dependency downloads require network access. Run these commands
from the repository root to configure and build the default unit runner:

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The build produces the `cot_cli` target as `build/src/cot`; `BUILD_CLI`
defaults to `ON`. Set it to `OFF` to build only the library and selected tests.
Ruby and Bundler are unnecessary for the default configuration.

Acceptance testing is optional. Install Ruby, Bundler, and `patch`, then run:

```sh
gem install bundler
bundle install
cmake --preset acceptance
cmake --build --preset acceptance
ctest --preset acceptance -L infrastructure
ctest --preset acceptance -L '^(unit|acceptance)$'
```

Shared configure, build, and test presets also provide `acceptance-only`,
`no-suites`, and `no-tests` selections. To configure and build without test
infrastructure:

```sh
cmake --preset no-tests
cmake --build --preset no-tests
ctest --preset no-tests
```

Schema fetching still runs with tests disabled. See the
[testing guide](docs/testing.md) for prerequisites, focused runs, preset
selections, and troubleshooting. Put local toolchain or cache overrides in
the ignored `CMakeUserPresets.json`, inheriting from shared presets and using
separate build directories.

## Install and consume

Build and install the library, public headers, CMake package, enabled CLI, and
its pinned static Expat dependency:

```sh
cmake --preset no-tests
cmake --build --preset no-tests
cmake --install build/no-tests
```

The shared prefix is `$HOME/.local`; `cmake --install build/no-tests --prefix
/path/to/prefix` overrides it. The installed tree may be moved together. Set
`CMAKE_PREFIX_PATH` to its current prefix when configuring a consumer:

```cmake
cmake_minimum_required(VERSION 3.24)
project(example LANGUAGES C CXX)
find_package(cot REQUIRED CONFIG)
add_executable(example main.c)
set_property(TARGET example PROPERTY C_STANDARD 11)
target_link_libraries(example PRIVATE cot::cot)
```

C11 source consumers enable C++ as a project language so CMake selects the
C++ linker and its runtime for the C++17 library implementation. C++17 consumers
may use `LANGUAGES CXX` and a `.cpp` source. The package locates Expat and links
its separate installed archive transitively. CPM source builds and the
`cot::cot` target remain available. Test dependencies are excluded from
installation.

## Pinned schemas

Ordinary builds do not download ATAK. To verify the pinned official
[ATAK-Civ release 5.5.1.8][release] schema sources explicitly, run:

```sh
cmake --preset schema-authority
cmake --build --preset schema-authority
ctest --preset schema-authority
```

This enables `COT_VERIFY_UPSTREAM_SCHEMA` and uses FetchContent to validate
all ten expected `.proto` files without building ATAK, downloading submodules
or Git LFS content, or modifying the schemas. The upstream checkout is larger
than the schema directory alone. No upstream checkout, Protobuf compiler, or
Protobuf runtime is needed to build or use the codec.

The dependency exports `COT_ATAK_PROTO_DIR` to the parent CMake scope as
`${cot_atak_civ_SOURCE_DIR}/commoncommo/core/impl/protobuf`. This variable
identifies the schema input and import directory for Protobuf generation.
Schema verification is independent of test options and defaults to off.
See the [design rationale](docs/design.md) for dependency and representation
choices.

## Documentation and contributing

- [API contract and C/C++ examples](docs/api.md)
- [CLI contract and shell workflows](docs/cli.md)
- [Design rationale and schema sources](docs/design.md)
- [Gherkin specifications](features/)
- [Testing](docs/testing.md)
- [Contributor rules](CONTRIBUTING.md)

Keep product coverage separate from infrastructure fixtures. This repository
supplies no project license; choose one before distribution and review
upstream notices.

[atak]: https://github.com/TAK-Product-Center/atak-civ
[release]: https://github.com/TAK-Product-Center/atak-civ/releases/tag/5.5.1.8
