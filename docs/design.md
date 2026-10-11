# CoT codec design

The library converts complete CoT events bidirectionally between XML and the
official TAK Protobuf payload. The [API contract](api.md) and
[CLI contract](cli.md) define its behavior. Expat parses XML, and the private
wire codec reads and writes the pinned Protobuf schema without exposing
generated types or requiring a Protobuf runtime.

## Schema authority and build boundary

The schema authority is official [ATAK-Civ release 5.5.1.8][release], commit
`7d583c8834f7f432da8dfc4a1e7c61d7df65e846`. The release tag is annotated;
the pinned hash identifies the underlying commit rather than the tag object.
[`cmake/dependencies/atak-civ/`](../cmake/dependencies/atak-civ/CMakeLists.txt)
uses FetchContent to populate the repository without configuring or building
ATAK. Submodule and Git LFS downloads are disabled. The schemas are not
modified or copied into the public API.

This source-only FetchContent dependency is an explicitly authorized exception
to the contributor policy's usual CPM dependency mechanism. Existing test
dependencies continue to use CPM. Schema verification is an explicit build
selection: `COT_VERIFY_UPSTREAM_SCHEMA` defaults to `OFF`, so ordinary builds
do not fetch ATAK. The wire codec does not consume generated sources or need
the upstream checkout at runtime. The `schema-authority` preset enables
verification independently of the test selections.

`COT_ATAK_PROTO_DIR` is exported to the parent CMake scope as
`${cot_atak_civ_SOURCE_DIR}/commoncommo/core/impl/protobuf`. It is the schema
**input and import directory** for Protobuf tooling, not an include directory
for generated C++ headers. Configuration validates these ten files:

```text
contact.proto
cotevent.proto
detail.proto
group.proto
precisionlocation.proto
status.proto
takcontrol.proto
takmessage.proto
takv.proto
track.proto
```

A missing schema fails verification configuration immediately. FetchContent's
normal source override and disconnected options support local development,
but an override must contain the same pinned sources to claim official-schema
verification. Initial population requires Git and network access and fetches
the upstream repository, which can be substantially larger than its schemas.
The wire codec requires no Protobuf compiler or runtime dependency.

## Public representation and ownership

Plain structs, counted spans, and free functions follow the contributor
rules. The C++17 header includes the C11 declarations, ensuring identical
contracts without adapter objects. A separate generated-message API would
expose code generation and runtime choices and make schema changes leak into
callers. The selected model instead exposes every pinned `CotEvent` field,
including the six standard details and extension bytes.

Counted spans avoid mandatory terminators and distinguish byte lengths from
character counts. `uint64_t` epoch milliseconds match the pinned schema's
unsigned time fields, including zero. Doubles preserve the upstream point
and track representation. Narrower XML limits are checked during XML
conversion, so a valid Protobuf payload can be inspected and forwarded even
when XML cannot express it. This costs callers an explicit unsupported-content
path instead of silently dropping or normalizing information.

Null nested pointers distinguish absence from a present message with default
scalars, following [proto3 message presence][presence]. A caller-created
event is a borrowed input tree. A decoded owner holds an immutable tree so
parsing needs only one matching destruction call, and no caller arena,
per-field frees, or generated types. Encoded bytes have a separate owner and
matching cleanup. This uses the familiar explicit allocation/release pairing
of APIs such as [SQLite's allocated result strings][sqlite-memory], while
keeping ownership in dedicated structs rather than mixing borrowed and owned
text spans. Allocation strategies remain private.

The API accepts full messages synchronously. Incremental parsers, streaming
state, transport negotiation, and recording policies do not improve the
selected single-message workflow and remain outside this contract. Sharing
immutable views is allowed; callers coordinate destruction and input mutation.
No mutable global configuration is needed.

## Native inspection and actionable losses

The read-only inspection companion exposes canonical native paths and stable
IDs, while keeping direct struct access as the low-level representation.
Integration tools resolve paths once and read native typed values by ID.
The catalog defines semantic availability centrally: optional empty text and
Undefined access are absent, required empty child strings follow group
presence, and numeric zero remains a value. This follows the documented CoT
mapping and [proto3 scalar/message presence][presence], without claiming to
recover unrepresented scalar wire presence. Exact unsigned getters prevent
timestamp precision loss through a generic floating-point registry. The
catalog includes residual XML and extension collections so omission reporting
can inventory every represented category of source data.

Structured codec diagnostics compose the existing status contract. They
identify a known canonical field/group and input location without exposing
parser handles, generated types, or owned error strings. Expat provides
[byte indexes][expat-position] for syntax errors and parsed element starts;
semantic attribute errors therefore point at their owning element. The wire
cursor records tag positions relative to the complete input, so nested
failures still have useful absolute offsets. Diagnostics are operation-local
and the decoded owner never retains caller diagnostic storage after return.

Successful Protobuf decoding reports discarded unknown fields and complete
wire byte counts. Unknown groups count once, preventing nested double
counting. The public representation still does not preserve those fields;
reporting loss lets integration tools make omissions visible without
inventing semantics for unknown payloads. An optional diagnostic output adds
no allocation or cleanup work, and the ordinary codec functions delegate to
the same diagnostic-capable operations.

## Detail fidelity and conversion limits

The pinned [detail schema][detail] requires whole-element mapping and
residual-XML precedence. Partial typed conversion would lose extra attributes
or children; the chosen fallback preserves the complete XML element instead.
Duplicated recognized elements also stay together in residual XML. On XML
output, a residual element suppresses its entire typed counterpart. This is
upstream behavior, not a library-specific conflict resolver.

Extensions preserve their IDs, bytes, duplicates, and order for Protobuf
forwarding. The [upstream protocol][protocol] makes extension interpretation
and negotiation application-specific. Inventing an XML wrapper for unknown
bytes would imply interoperability the schema does not provide, so XML
conversion explicitly fails when extensions are present. Unknown wire fields
outside that explicit array are not included in the preservation promise.

Semantic round trips are the useful contract: represented values and message
presence survive where both formats can represent them. Byte equality would
require retaining XML formatting and Protobuf serialization choices that are
irrelevant to those values. See the API contract for defaults, XML access
normalization, residual precedence, and numeric limits.

## CLI composition and output safety

The `cot_cli` target emits `cot` when `BUILD_CLI` is enabled (default on).
XML is the editable input/output form, while Protobuf is an opaque binary
payload. Both CLI
commands compose decode, get, and encode from the public API, so the tool
cannot develop a competing conversion policy.

Reading through EOF gives files and pipes identical message boundaries.
Completing conversion before output preparation protects destinations from
codec failure and allows the input and output path to be the same. A private
I/O boundary supplies temporary creation, replacement, and discard operations
so failure behavior can be checked without relying on filesystem accidents.

Named output stages a full result in an exclusive sibling temporary, then
writes, flushes, and closes its stream before atomic replacement. On POSIX
unpublished temporaries remain private (`0600`). A transaction retains a file
descriptor to apply the original regular destination's permission bits only
when the bytes are complete. It verifies the named temporary still identifies
the held file, closes the final descriptor, and replaces the destination.
Published transactions discard only their memory; they do not remove the old
temporary pathname, which may already belong to another writer.

Failure before publication leaves the destination unchanged. Discard attempts
to remove an unpublished temporary and reports cleanup failure. Symlink and
nonregular destinations are rejected. Atomic visibility does not promise
power-loss durability or preserve file identity, ownership, ACLs, extended
attributes, or hard-link relationships. Concurrent writers and path changes
require caller coordination. Stdout has staged conversion but ordinary stream
write failure semantics. Binary I/O and stderr separation keep pipelines
usable across platforms.

## Verification

The product specifications under [`features/`](../features/) execute native
steps against the public API and the CLI. Unit tests cover argument checking,
conversion, ownership, malformed input, and allocation failures. Acceptance
steps also exercise real subprocesses and pipes, with a private I/O boundary
for deterministic read, write, flush, and close failures. No product steps are
placeholders.

Verify schema population, the pinned commit, all ten files, the parent-scope
path, and repeated configuration. Exercise the five shared presets and the
infrastructure checks required by [CONTRIBUTING.md](../CONTRIBUTING.md).
Ruby, Bundler, and the locked gems are required for the native acceptance
runner; a missing prerequisite is an error. Keep local cache or toolchain
overrides in ignored user presets.

[release]: https://github.com/TAK-Product-Center/atak-civ/releases/tag/5.5.1.8
[detail]: https://github.com/TAK-Product-Center/atak-civ/blob/7d583c8834f7f432da8dfc4a1e7c61d7df65e846/commoncommo/core/impl/protobuf/detail.proto
[protocol]: https://github.com/TAK-Product-Center/atak-civ/blob/7d583c8834f7f432da8dfc4a1e7c61d7df65e846/commoncommo/core/impl/protobuf/protocol.txt
[presence]: https://protobuf.dev/programming-guides/field_presence/
[sqlite-memory]: https://www.sqlite.org/c3ref/free.html

[expat-position]: https://libexpat.github.io/doc/api/latest/
