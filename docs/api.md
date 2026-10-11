# CoT API contract

The public declarations are in [`cot.h`](../include/cot/cot.h) (C11) and
[`cot.hpp`](../include/cot/cot.hpp) (procedural C++17). Both expose the same
plain types and free functions, with C linkage. The native codec implements
the contract below; unit tests and native Gherkin steps verify its behavior.

## Formats and message boundary

`COT_FORMAT_XML` is one UTF-8 CoT 2.0 `event` document, with an optional XML
1.0 declaration. `COT_FORMAT_PROTOBUF` is one serialized official
`atakmap.commoncommo.protobuf.v1.TakMessage` containing `cotEvent` and no
`takControl`. No TAK magic bytes, length prefix, or transport framing is
included. A control-only message, event plus control (even an empty control
message), or a message with neither nested message is unsupported.
A truncated or otherwise invalid wire encoding is malformed.

Decoding consumes the entire supplied byte span. XML permits surrounding
whitespace but rejects a second document or trailing non-whitespace. Protobuf
uses normal proto3 parsing rules, including default scalars, merged singular
messages, and unknown fields. Concatenated Protobuf serializations can parse
as one merged message: this API cannot recover their original boundaries.
Callers must supply one payload. Unknown Protobuf fields outside the explicit
extension array are not part of the public preservation guarantee.

XML has exactly one `point`, at most one `detail`, and the required event
attributes `version`, `type`, `uid`, `time`, `start`, `stale`, and `how`.
`version` must be `2.0`; `type`, `uid`, and `how` must be nonempty.
Missing required attributes, point coordinates, duplicate point/detail
elements, or invalid lexical values are malformed. A well-formed document using a different event version,
unknown event/point attributes, other event children, or namespaces on core
elements is unsupported rather than silently losing content. Point children
and non-namespace detail attributes are unsupported. Arbitrary detail children
are retained; namespace declarations needed by residual children travel with
those children when serialized. DTDs and entity declarations are unsupported; external
resources are never fetched. Predefined and numeric XML entities are supported.

## Plain data model

`cot_text` is a byte-counted UTF-8 span, without a required terminator.
`cot_bytes` is an uninterpreted byte span. In either type a null data pointer
is legal only at size zero; a nonnull pointer with size zero is also empty.
Only the counted bytes are read. Embedded NUL and other valid UTF-8 code
points outside XML 1.0's character repertoire can be preserved in Protobuf
strings but cannot be emitted as XML. Invalid UTF-8 is rejected.

`cot_event` models every field in the pinned
[`CotEvent` schema][event-schema]. Scalar proto3 fields have no independent
presence flag. Empty optional strings mean absent, and zero numbers remain
values. The mapping is:

| Public field | Protobuf field | XML |
| --- | --- | --- |
| `type`, `uid`, `how` | same names | event attributes |
| `access`, `qos`, `opex` | same names | optional event attributes |
| `caveat` | `caveat` | optional event attribute |
| `releasable_to` | `releasableTo` | event `releasableTo` |
| `send_time_ms` | `sendTime` | event `time` |
| `start_time_ms` | `startTime` | event `start` |
| `stale_time_ms` | `staleTime` | event `stale` |
| `point.latitude`, `point.longitude` | `lat`, `lon` | point `lat`, `lon` |
| `point.hae`, `point.ce`, `point.le` | `hae`, `ce`, `le` | point attributes |
| `detail` | `detail` | detail children |

An absent or empty `access` and the literal `Undefined` have the same CoT
meaning. XML decode normalizes `Undefined` to empty, Protobuf encoding omits
that default, and XML output omits empty access. Other optional empty event
attributes are omitted on XML output too.

Timestamps are `uint64_t` milliseconds since 1970-01-01T00:00:00Z.
There is no implicit clock read, expiry check, or ordering constraint among
the three timestamps. Protobuf preserves the full unsigned domain. XML input
accepts Gregorian RFC 3339 timestamps with UTC `Z` or numeric offsets and
normalizes them to UTC. XML output uses `YYYY-MM-DDTHH:MM:SS.sssZ`.
Only UTC instants from epoch zero through 9999-12-31T23:59:59.999Z
(`253402300799999`) are representable as XML in this API. Valid earlier/later
instants, leap-second notation, and nonzero submillisecond precision are
unsupported; invalid calendar dates are malformed. Extra fractional digits
that are all zero after milliseconds are accepted without loss.

`cot_point` uses WGS84 latitude and longitude in degrees, HAE (height above
the ellipsoid) in metres, and circular and linear error in metres.
`COT_POINT_UNKNOWN_METRES` is exactly `999999.0`, the upstream unknown HAE,
CE, and LE sentinel. Zero is a real value, never an absence marker.
XML representation requires finite latitude in [-90, 90], longitude in
[-180, 180], finite HAE, and finite nonnegative CE and LE. Protobuf preserves
other double values; XML conversion of them is unsupported. Round trips
promise numeric equality, not a particular decimal spelling, signed-zero
spelling, or NaN payload bits.

## Details, presence, and extensions

A null `detail` pointer means no nested `Detail` message. A nonnull pointer
with all-zero members means a present empty message. The same rule applies
to the six typed detail pointers. Protobuf decode and encode preserve this
presence even when every scalar in a present message has its proto3 default.
XML without detail maps to null; `<detail/>` maps to a present empty detail.

`cot_detail.xml` maps to `xmlDetail`: UTF-8 serialized children of `detail`,
without the wrapper or XML declaration. It can contain multiple elements and
mixed text. XML output parses it as a fragment under a temporary detail root.
An invalid fragment or characters that XML cannot represent cause unsupported
content on XML encoding; Protobuf conversion retains the string opaquely.

The [upstream detail mapping][detail-schema] converts a recognized element
only if the whole element is expressible by the typed message:

| Pointer/type | XML element | Required attributes | Optional attributes |
| --- | --- | --- | --- |
| `contact` / `cot_contact` | `contact` | `callsign` | `endpoint`, `altendpoints` |
| `group` / `cot_group` | `__group` | `name`, `role` | none |
| `precision_location` / `cot_precision_location` | `precisionlocation` | `geopointsrc`, `altsrc` | none |
| `status` / `cot_status_detail` | `status` | `battery` | none |
| `takv` / `cot_takv` | `takv` | `device`, `platform`, `os`, `version` | none |
| `track` / `cot_track` | `track` | `speed`, `course` | none |

Required string attributes must exist but may be empty, preserving present
messages with default strings. Empty optional contact attributes collapse to
absence. Battery is an unsigned 32-bit integer; the full domain is preserved,
without clamping to a battery percentage. Track speed is metres per second;
course is degrees clockwise from true north. Typed XML track output requires
finite speed and course. Finite values are preserved without clamping speed
or wrapping course; nonfinite Protobuf track values are retained but are
unsupported for XML output.

A missing required attribute, unmappable value, extra attribute, child node
(other than formatting whitespace), or multiple occurrences of the same
recognized element causes **all** occurrences of that element to remain
whole in residual XML. The corresponding typed pointer stays null. These
conditions inside an otherwise well-formed detail do not fail the event.
Unrecognized or namespaced detail elements remain residual XML too. A typed
mapping removes its entire source element from the residual fragment.

For Protobuf or caller-created events that contain both residual XML and a
typed message for the same element, XML output preserves the residual element
and ignores the entire corresponding typed message. The suppressed typed
value need not be XML-representable. The decoded view and Protobuf re-encoding
retain both representations; they do not normalize away the conflict.

`cot_extension` holds a `uint32_t id` and opaque `cot_bytes data`.
`cot_detail.extensions` is an ordered array of `extension_count` entries;
null requires count zero. Order, repeated IDs, zero IDs, empty bytes, and all
byte values survive Protobuf decoding and encoding. The library does not
register IDs or interpret extension payloads. Even an empty extension entry
makes XML encoding unsupported: there is no extension-to-XML decoder in this
contract. Upstream deployments must use registered IDs and negotiate recipient
support; this codec does not perform [protocol negotiation][protocol].

## Semantic inspection for integrations

[`inspect.h`](../include/cot/inspect.h) and its procedural C++17 forwarding
header [`inspect.hpp`](../include/cot/inspect.hpp) expose a read-only native
field catalog. Integration code can resolve each configured canonical path
once with `cot_field_resolve`, then use its stable `cot_field_id` with
`cot_field_get`. No generated types, manual struct registry, conversion to
floating point, or allocation is required. Matching is exact and
case-sensitive. Unknown paths resolve to `COT_FIELD_INVALID`.

`cot_field_count()` returns the number of entries; valid IDs are the integers
from one through that count. `cot_field_get_info(id)` returns static immutable
metadata, or null for an invalid ID. Metadata includes the canonical path,
native type, units, containing group, and whether empty means absent. Paths
follow the public struct spelling: for example `send_time_ms`,
`point.latitude`, `detail.contact.callsign`, `detail.xml`, and
`detail.extensions`. Timestamp getters return exact `uint64_t`, battery
returns its full `uint32_t` domain in the unsigned union member, and double
getters retain the native double. Units describe values without clamping,
conversion, or additional validation. Text remains counted UTF-8, including
embedded NUL. Extensions remain the complete ordered native collection.

`cot_field_type_string`, `cot_field_unit_string`, and `cot_group_id_string`
return stable descriptive labels for metadata without requiring a caller-owned
enum registry. Their inputs are the corresponding public enum values. Native
type labels are `text`, `uint64`, `uint32`, `double`, and `extensions`; unit
labels are `none`, `utc_milliseconds`, `degrees`, `metres`,
`metres_per_second`, and `percent`. Group labels are canonical paths: `event`,
`point`, `detail`, `detail.contact`, `detail.group`,
`detail.precision_location`, `detail.status`, `detail.takv`, and
`detail.track`. Unknown enum representations return null. Returned text is
terminated, immutable, and valid for the process lifetime. These helpers do
not allocate, perform I/O, mutate global state, or depend on locale;
independent and concurrent calls are safe.

`cot_field_get(event, id, out)` accepts a valid borrowed event, including a
caller-created event. It resets `out`, sets its native type, and reports
semantic availability independently of the union value. An absent nested
group makes its fields unavailable. Empty optional event/contact attributes,
empty residual XML, and an empty extension collection are unavailable.
`access="Undefined"` is unavailable too. Required strings are available even
when empty, provided their group exists. All numeric fields, including zero,
are available when their group exists. The getters do not recover original
scalar wire presence or validate the event's suitability for XML output.

`cot_group_is_present(event, group, out)` queries the public struct boundary:
EVENT and POINT always exist, DETAIL follows the detail pointer, and the six
typed groups follow their nested pointers. Present empty messages stay
present. Null arguments or invalid IDs return `COT_STATUS_INVALID_ARGUMENT`;
non-null getter/query outputs are reset even on error. Reads do not mutate
or allocate. Metadata lasts for the process; returned text and extension
views borrow the input event and become invalid when its storage does.
Independent calls and concurrent reads of immutable events are safe.

A minimal mapping read preserves both absence and unsigned precision:

```c
#include <cot/inspect.h>

cot_status read_send_time(const cot_event *event, cot_field_value *value)
{
	cot_field_id id = cot_field_resolve("send_time_ms");

	return cot_field_get(event, id, value);
}
```

After checking success and `value.available`, use `value.value.uint_value`.
For text fields use `value.value.text`, for double fields use
`value.value.real`, and for `detail.extensions` use
`value.value.extensions`. Cache resolved IDs alongside integration mappings.
Keep the event owner alive throughout every read of returned borrowed data.

## Structured codec diagnostics

`cot_encode_ex` and `cot_decode_ex` have the same behavior and ownership
requirements as `cot_encode` and `cot_decode`, with an optional
`cot_diagnostic *` output. They reset that output at entry on every call.
Diagnostic storage must not overlap inputs or outputs. The ordinary codec
calls compose these functions with a null diagnostic, so parsing and
validation policy remain identical.

The returned status determines success. On failure the diagnostic identifies
a machine-readable reason and, where known, a static canonical field/group
path and a zero-based byte offset in the supplied input. XML semantic errors
identify the offending owning element's opening byte, rather than promising
an attribute-value token location. Protobuf errors identify the offending
field tag, including inside nested messages. Syntax errors may have a byte
location without a field path. Unsupported envelope/control content identifies
its group; allocation and API-argument errors may lack a path and location.
`COT_DIAGNOSTIC_NO_OFFSET` means no source location is available. Encoding
always uses that sentinel. Diagnostic strings require no cleanup and remain
valid after decoded-owner destruction.

On successful Protobuf decoding, `unknown_protobuf_fields` and
`unknown_protobuf_bytes` report every discarded unknown wire field, including
unknown fields inside known nested messages and known tags with the wrong
wire type. The byte count includes tags and values. A whole unknown group
counts as one field with all its bytes. On failure the counters describe only
successfully skipped fields in the examined prefix; they do not imply
successful or complete decoding. XML decoding and all encoding reset both
counters to zero. Callers can include successful-decode counters in a loss
report without attempting to preserve or interpret those fields.

The reason enum distinguishes API arguments, malformed XML, malformed
Protobuf, missing required data, invalid field values, unsupported content,
and allocation failure. A successful operation has reason
`COT_DIAGNOSTIC_NONE`, even when discarded-field counters are nonzero.
Reasons and paths supplement the status contract; a reason such as
`INVALID_VALUE` may accompany malformed input, an invalid caller value, or a
value unsupported by the target format. No diagnostic owns storage, invokes
callbacks, or changes codec cleanup or concurrency requirements.

## Functions, ownership, and errors

`cot_encode(format, event, out)` borrows the event and every reachable span,
struct, and array only for the duration of the call. The caller keeps them
alive and unchanged. On success `out` owns library-allocated result bytes of
exactly `out->size`; no NUL terminator is promised. Use
`cot_buffer_destroy(&buffer)` to free them, never `free` or `delete`.
The encoded result is independent of the input after return.

`cot_decode(format, data, size, out)` borrows the bytes for the call and on
success writes a new opaque `cot_decoded_event *`. The caller may immediately
release or overwrite the input. `cot_decoded_event_get(owner)` returns a
borrowed immutable `const cot_event *`, including all nested data. Repeated
gets return the same view; reads and encoding do not invalidate it. The owner
must remain alive until all reads and encodes using the view finish.
`cot_decoded_event_destroy(owner)` releases it and invalidates every nested
pointer. Null owners are harmless for get (returns null) and destroy.
Destroy a nonnull owner exactly once; a caller variable is not reset by destroy.

An encode output must initially be `{NULL, 0}`; a decode output must initially
point to a null owner. Null output pointers, nonempty outputs, null event
pointers, inconsistent pointer/count pairs, and unknown format selectors
return `COT_STATUS_INVALID_ARGUMENT`. A nonempty output is rejected unchanged;
it is not automatically freed. On any failure, a valid empty output remains
empty, and all temporary library allocations are released. Empty decode input
(including null data with zero size) is a malformed XML message, or an
unsupported empty Protobuf envelope. Inputs and outputs must not overlap.
All pointers must reference suitably aligned live objects and readable spans;
arbitrary dangling pointers, forged handles, overlap, and size overflow in
caller storage are outside the detectable-invalid-argument guarantee.

| Status | Meaning |
| --- | --- |
| `COT_STATUS_OK` | Complete result produced |
| `COT_STATUS_INVALID_ARGUMENT` | Invalid API arguments as above, including invalid UTF-8 in caller text |
| `COT_STATUS_MALFORMED_MESSAGE` | Invalid XML/wire/UTF-8 input, missing required XML data, or invalid core lexical value |
| `COT_STATUS_UNSUPPORTED_CONTENT` | Parsed content outside the representation contract, including XML-inexpressible values on encoding |
| `COT_STATUS_ALLOCATION_FAILURE` | Library could not allocate required storage |

Wire parsing takes precedence over content checks; malformed wire data is not
reported as unsupported merely because it contains control data. Protobuf
scalar defaults are accepted on decode, including empty core strings, which
can subsequently be unsupported for XML encoding. An XML core numeric value
that parses but exceeds an XML-representation range is unsupported. These
rules do not reject an unfamiliar CoT type or how code merely for its spelling.

`cot_buffer_destroy(buffer)` accepts null, releases an owned buffer, and
resets both fields to zero. Calling it again on the reset buffer is harmless.
Only empty or library-owned buffers may be passed. Copying a buffer struct
does not duplicate its allocation. `cot_status_string(status)` returns static,
immutable, NUL-terminated English text, never null; unknown enum values return
`"unknown status"`. Its storage lasts for the process. Statuses drive program
logic; wording for known statuses is diagnostic, not a stable protocol.

Calls do not throw across the API, use `errno` as their error contract, perform
I/O, invoke callbacks, or consult mutable global configuration. Encoding and
decoding are synchronous and may allocate memory proportional to the complete
message. Independent operations can run concurrently. Multiple threads may
read or encode the same immutable owner while it remains alive. Callers must
synchronize destruction, buffer mutation, and changes to caller-owned inputs;
there is no thread affinity or per-thread initialization.

Only the represented fields have a semantic round-trip guarantee. XML
whitespace, declaration presence, quoting, attribute order, and placement of
typed children relative to residual children may change. Protobuf ordering,
unknown fields, and original wire bytes are not guaranteed. Source contracts
currently target C11/C++17; no released cross-version binary ABI is promised.

## C construction and encoding

A zero-initialized struct supplies scalar defaults, not a complete XML event.
This C11 example supplies required core fields and explicit unknown errors:

```c
#include <cot/cot.h>

cot_status make_event(cot_buffer *result)
{
	cot_contact contact = {0};
	contact.callsign = (cot_text){"Alpha", 5};
	cot_detail detail = {0};
	detail.contact = &contact;
	cot_event event = {0};
	event.type = (cot_text){"a-f-G-U-C", 9};
	event.uid = (cot_text){"alpha-1", 7};
	event.how = (cot_text){"m-g", 3};
	event.send_time_ms = 1704067200123ULL;
	event.start_time_ms = event.send_time_ms;
	event.stale_time_ms = event.send_time_ms + 60000;
	event.point.latitude = 38.0;
	event.point.longitude = -77.0;
	event.point.hae = COT_POINT_UNKNOWN_METRES;
	event.point.ce = COT_POINT_UNKNOWN_METRES;
	event.point.le = COT_POINT_UNKNOWN_METRES;
	event.detail = &detail;
	return cot_encode(COT_FORMAT_PROTOBUF, &event, result);
}
```

The caller starts with `cot_buffer result = {0}`, checks the returned status,
uses the counted bytes on success, then calls `cot_buffer_destroy(&result)`.
Encoding copies what it needs before the stack objects above cease to exist.

## Parsing and conversion in C++17

This free function implements the library portion of either CLI direction.
The tool supplies input bytes, chooses the formats, and handles file I/O.
`result` must be an empty buffer. Changing the input to Protobuf and output to
XML performs `cot decode`; the reverse performs `cot encode`.

```cpp
#include <cot/cot.hpp>

cot_status convert(cot_format input_format, const uint8_t *input,
	size_t size, cot_format output_format, cot_buffer *result)
{
	cot_decoded_event *owner = nullptr;
	cot_status status = cot_decode(input_format, input, size, &owner);

	if (status != COT_STATUS_OK)
		return status;
	const cot_event *view = cot_decoded_event_get(owner);

	status = cot_encode(output_format, view, result);
	cot_decoded_event_destroy(owner);
	return status;
}
```

The same decode/get/destroy sequence parses an event for inspection without
encoding. Read fields through `view` only before destroying `owner`. Check
`status` before accessing any result. On encode failure the owner still needs
destruction, as shown. Report failures using `cot_status_string(status)`;
cleanup remains valid with an empty output buffer. Neither example requires
generated Protobuf headers, STL types, exceptions, or library initialization.

[event-schema]: https://github.com/TAK-Product-Center/atak-civ/blob/7d583c8834f7f432da8dfc4a1e7c61d7df65e846/commoncommo/core/impl/protobuf/cotevent.proto
[detail-schema]: https://github.com/TAK-Product-Center/atak-civ/blob/7d583c8834f7f432da8dfc4a1e7c61d7df65e846/commoncommo/core/impl/protobuf/detail.proto
[protocol]: https://github.com/TAK-Product-Center/atak-civ/blob/7d583c8834f7f432da8dfc4a1e7c61d7df65e846/commoncommo/core/impl/protobuf/protocol.txt
