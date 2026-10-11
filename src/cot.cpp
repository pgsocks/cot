#include <cot/cot.hpp>
#include "internal.h"
#include <cstring>
#include <new>

namespace
{

static void reset_diagnostic(cot_diagnostic *diagnostic)
{
	if (diagnostic)
		*diagnostic = {COT_DIAGNOSTIC_NONE, nullptr,
			COT_DIAGNOSTIC_NO_OFFSET, 0, 0};
}

static cot_status finish_diagnostic(cot_diagnostic *diagnostic,
	cot_status status, cot_format format)
{
	cot_diagnostic_reason reason = COT_DIAGNOSTIC_NONE;

	switch (status) {
	case COT_STATUS_OK:
		return status;
	case COT_STATUS_INVALID_ARGUMENT:
		reason = COT_DIAGNOSTIC_INVALID_ARGUMENT;
		break;
	case COT_STATUS_MALFORMED_MESSAGE:
		reason = format == COT_FORMAT_XML
				 ? COT_DIAGNOSTIC_MALFORMED_XML
				 : COT_DIAGNOSTIC_MALFORMED_PROTOBUF;
		break;
	case COT_STATUS_UNSUPPORTED_CONTENT:
		reason = COT_DIAGNOSTIC_UNSUPPORTED_CONTENT;
		break;
	case COT_STATUS_ALLOCATION_FAILURE:
		reason = COT_DIAGNOSTIC_ALLOCATION_FAILURE;
		break;
	}
	cot_diagnostic_error(diagnostic, reason, nullptr,
		COT_DIAGNOSTIC_NO_OFFSET);
	return status;
}

} // namespace

/*
 * Introduce private plain structs for a parse cursor, checked output
 * writer, allocation ledger, and decoded owner. Keep generated schema
 * types private if a compatible representation is selected. Do not require
 * STL, exceptions, mutable global configuration, initialization, or thread
 * state.
 *
 * The owner must hold one stable cot_event view and every referenced text,
 * nested struct, extension array, and byte span. Populate private storage
 * before publishing pointers. Presence flags must remain independent of
 * scalar defaults; a present empty nested message needs a nonnull pointer.
 *
 * Centralize checked size addition/multiplication; route allocation
 * and release through the private cot_allocate/cot_release seam. Associate
 * every partial allocation with the current operation's ledger. Failure
 * unwinds the entire ledger without publishing output. Transfer ownership
 * only at the final commit point. Link-time test replacement must remain
 * private and must not introduce production mutable global configuration
 * or alter the public API.
 *
 * Separate the detailed algorithms below into private boundaries:
 * - validate_event checks a borrowed event for the target format and returns
 *   argument/representation status without allocating or changing outputs.
 * - parse_xml_event/parse_protobuf_event consume the complete borrowed span
 *   into operation-local state; neither publishes an owner or borrows input
 *   beyond the call. Temporary allocations belong to the operation ledger.
 * - prepare_xml_detail resolves residual precedence before validating typed
 *   values that will be emitted; its prepared state remains operation-local.
 * - materialize_owner copies parsed state into stable owned storage and
 *   publishes the view only after every nested allocation is complete.
 * - release_owner/rollback share ledger cleanup for partial construction
 *   and destruction, releasing owned storage without touching borrowed data.
 */

cot_status cot_encode_ex(cot_format format, const cot_event *event,
	cot_buffer *out, cot_diagnostic *diagnostic)
{
	/*
	 * Validate arguments before touching output or allocating:
	 * - Require out, then reject either nonzero output member unchanged.
	 * - Require event and a known format selector.
	 * - Validate all reachable pointer/count pairs, including each text,
	 *   residual XML, extension array, and extension byte span. A nonnull
	 *   data pointer with zero size is legal. Do not scan beyond counts.
	 * - Validate UTF-8 in every caller text span. Invalid caller UTF-8 is
	 *   invalid argument even if the target could omit a field. Embedded
	 *   NUL is valid UTF-8 and has separate XML representation checks.
	 * Aliased or dangling caller storage is outside the detection promise.
	 * Never read a clock, change caller inputs, perform I/O, or use errno
	 * as the error channel. All operation state stays private to this call.
	 *
	 * For Protobuf, size and write an unframed official TakMessage
	 * containing only cotEvent. Map all CotEvent scalars, including full
	 * uint64 timestamps, empty core text, and all double values. Omit
	 * default scalars without losing the presence of detail or any typed
	 * nested message. Normalize literal Undefined access to the empty
	 * default as required by the CoT access mapping.
	 *
	 * Copy detail.xml as counted text without interpreting its fragment.
	 * Preserve all six typed messages alongside residual XML, including
	 * conflicting representations and present messages with all defaults.
	 * Preserve each extension's id and bytes in array order, including
	 * duplicates, zero IDs, empty data, and every byte value. No extension
	 * interpretation, framing, or transport negotiation belongs here.
	 *
	 * For XML, reject any extension entry before emitting bytes.
	 * Require nonempty type, uid, and how, and XML 1.0 characters in all
	 * text that will be emitted. Empty optional attributes are omitted;
	 * omit empty or Undefined access. Escape attribute and text values
	 * without assuming a terminator or preserving lexical formatting.
	 *
	 * Require epoch milliseconds no later than 253402300799999, then
	 * convert each independently to YYYY-MM-DDTHH:MM:SS.sssZ. Enforce no
	 * time ordering or expiry policy. Require finite latitude/longitude
	 * within [-90, 90]/[-180, 180], finite HAE, and finite nonnegative
	 * CE/LE. Preserve zero and the 999999.0 unknown-metres sentinel as
	 * values. Emit enough decimal precision for semantic double equality.
	 *
	 * Parse residual detail.xml under a temporary detail root; reject an
	 * invalid fragment or unrepresentable characters as unsupported.
	 * Never fetch external resources; reject declarations requiring a DTD
	 * or entities. Retain mixed text, child elements, and needed namespace
	 * declarations. Detect recognized residual elements before checking
	 * typed XML representation: an element suppresses the entire matching
	 * typed message, even when its suppressed values cannot become XML.
	 * Namespaced elements do not become standard unqualified elements.
	 *
	 * Emit only unsuppressed typed messages, preserving present default
	 * strings with required empty attributes. Omit empty optional contact
	 * attributes. Battery retains its full uint32 domain; finite speed and
	 * course are emitted without clamping or wrapping. Nonfinite emitted
	 * track values are unsupported. Preserve absent versus present empty
	 * detail through omission versus an empty detail element.
	 *
	 * Keep the checked-size calculation and byte writing consistent.
	 * Allocate result storage with cot_allocate and serialize privately;
	 * the span has exactly the encoded size with no promised terminator.
	 * On any validation, representation, or allocation failure release
	 * temporaries and return the precise status with out still empty.
	 * Commit both output members only once encoding is fully successful.
	 */
	reset_diagnostic(diagnostic);
	const cot_format xml = COT_FORMAT_XML;
	const cot_format protobuf = COT_FORMAT_PROTOBUF;

	if (!out || out->data || out->size || !event ||
		(memcmp(&format, &xml, sizeof(format)) &&
			memcmp(&format, &protobuf, sizeof(format))))
		return finish_diagnostic(diagnostic,
			COT_STATUS_INVALID_ARGUMENT, COT_FORMAT_XML);
	cot_status status = cot_validate_event(event);

	if (status != COT_STATUS_OK) {
		cot_diagnose_event(event, COT_FORMAT_PROTOBUF, diagnostic);
		return finish_diagnostic(diagnostic, status, format);
	}
	cot_writer writer{};

	writer.diagnostic = diagnostic;
	if (format == COT_FORMAT_XML)
		cot_write_xml(&writer, event);
	else
		cot_write_protobuf(&writer, event);
	if (writer.status != COT_STATUS_OK) {
		status = writer.status;
		cot_writer_destroy(&writer);
		if (status == COT_STATUS_UNSUPPORTED_CONTENT)
			cot_diagnose_event(event, format, diagnostic);
		return finish_diagnostic(diagnostic, status, format);
	}
	out->data = writer.data;
	out->size = writer.size;
	return COT_STATUS_OK;
}

cot_status cot_decode_ex(cot_format format, const uint8_t *data, size_t size,
	cot_decoded_event **out, cot_diagnostic *diagnostic)
{
	/*
	 * Require out, then reject a nonnull *out unchanged. Require a
	 * known format and data whenever size is nonzero. Leave a valid null
	 * output null on every failure. Only the counted input bytes are read;
	 * retain no pointers into them after return. Empty XML is malformed;
	 * an empty Protobuf envelope is unsupported.
	 *
	 * Parse the complete Protobuf wire span before content checks.
	 * Validate tags, varints, field lengths, packed representations where
	 * allowed by the pinned schemas, and nested message boundaries without
	 * integer overflow or reads beyond the span. Truncated/invalid wire
	 * and invalid string UTF-8 are malformed even when control is present.
	 * Apply normal proto3 defaults, singular scalar replacement, singular
	 * message merging, repeated extension order, and unknown-field parsing.
	 * Concatenated serializations may form one merged message; do not
	 * invent boundaries. Unknown fields need not survive re-encoding.
	 *
	 * After successful parsing, require cotEvent presence and reject any
	 * takControl presence, including an empty control. Decode every event
	 * field and typed detail with independent message presence. Preserve
	 * full uint64 timestamps, all doubles, and valid UTF-8 text including
	 * XML-inexpressible code points. Keep xmlDetail opaque and retain
	 * typed/residual conflicts. Copy ordered extension IDs and bytes.
	 *
	 * Parse XML as one UTF-8 document with an optional XML 1.0
	 * declaration. Consume the full span; allow surrounding whitespace,
	 * reject a second document or other trailing content as malformed.
	 * Support predefined/numeric entities. Never fetch external resources;
	 * DTDs and entity declarations are unsupported. Distinguish malformed
	 * syntax/UTF-8 from well-formed but unsupported representation.
	 *
	 * Require an unnamespaced event, version/type/uid/time/start/stale/how,
	 * nonempty type/uid/how, exactly one point with all coordinates, and
	 * at most one detail. Missing data, duplicate point/detail, and invalid
	 * core lexical values are malformed. Other well-formed versions,
	 * unknown event/point attributes or event children, namespaces on core
	 * elements, point children, and non-namespace detail attributes are
	 * unsupported. Normalize literal Undefined access to empty text.
	 *
	 * Parse Gregorian RFC 3339 timestamps, validate calendar dates, apply
	 * Z/numeric offsets, and normalize UTC using checked arithmetic. No
	 * clock or relative ordering participates. Invalid dates are malformed;
	 * valid out-of-domain instants, leap seconds, and nonzero precision
	 * below milliseconds are unsupported. Accept extra fractional zeroes.
	 * Parse point numbers without locale-dependent behavior. A valid
	 * numeric value outside XML ranges is unsupported; retain zero and
	 * the documented unknown sentinel without treating either as absent.
	 *
	 * Inventory all detail children before mapping any recognized
	 * name. For contact, __group, precisionlocation, status, takv, and
	 * track, use the exact required/optional attribute sets in docs/api.md.
	 * Required string attributes may be empty. Battery must parse as uint32
	 * and track values must be finite without clamping or normalization.
	 *
	 * Map a recognized name only when it occurs exactly once and its
	 * entire element is representable: no extra attributes, missing
	 * required attributes, unmappable values, or children except formatting
	 * whitespace. Otherwise keep every occurrence whole in residual XML
	 * and leave the matching typed pointer null. Such fallback never fails
	 * an otherwise valid event. Preserve unknown/namespaced children and
	 * mixed residual text, carrying namespace declarations where needed.
	 * Remove the entire source element only after its typed mapping wins.
	 *
	 * Materialize a private owner only from the fully parsed event.
	 * Use cot_allocate to copy text, byte spans, extension arrays, nested
	 * structs, and the stable view. Preserve absent versus present empty
	 * detail and typed messages. No owner pointer may refer to input or
	 * parser scratch, freed storage, or storage moved after publication.
	 * Check all storage sizes and unwind partial allocations on failure.
	 * Commit *out only after the owner and all its views are complete.
	 */
	reset_diagnostic(diagnostic);
	const cot_format xml = COT_FORMAT_XML;
	const cot_format protobuf = COT_FORMAT_PROTOBUF;

	if (!out || *out || (!data && size) ||
		(memcmp(&format, &xml, sizeof(format)) &&
			memcmp(&format, &protobuf, sizeof(format))))
		return finish_diagnostic(diagnostic,
			COT_STATUS_INVALID_ARGUMENT, COT_FORMAT_XML);
	if (!size)
		return finish_diagnostic(diagnostic,
			format == COT_FORMAT_XML ? COT_STATUS_MALFORMED_MESSAGE
						: COT_STATUS_UNSUPPORTED_CONTENT,
			format);
	void *storage = cot_allocate(sizeof(cot_decoded_event));

	if (!storage)
		return finish_diagnostic(diagnostic,
			COT_STATUS_ALLOCATION_FAILURE, format);
	cot_decoded_event *owner = new (storage) cot_decoded_event{};

	owner->diagnostic = diagnostic;
	cot_status status = format == COT_FORMAT_XML
				    ? cot_parse_xml(owner, data, size)
				    : cot_parse_protobuf(owner, data, size);

	if (status != COT_STATUS_OK) {
		cot_decoded_event_destroy(owner);
		return finish_diagnostic(diagnostic, status, format);
	}
	owner->diagnostic = nullptr;
	*out = owner;
	return COT_STATUS_OK;
}

const cot_event *cot_decoded_event_get(const cot_decoded_event *owner)
{
	/*
	 * Return null for a null owner; otherwise borrow its stable event
	 * view without allocation or mutation. Repeated gets return the same
	 * address. Concurrent reads/encodes are safe while callers keep the
	 * immutable owner alive and synchronize destruction. No thread affinity
	 * or global state belongs in this lookup.
	 */
	return owner ? &owner->event : nullptr;
}

void cot_decoded_event_destroy(cot_decoded_event *owner)
{
	/*
	 * Treat null as harmless. For a live owner, release all owned
	 * backing storage exactly once using cot_release, then release the
	 * owner through the same seam. Every borrowed view becomes
	 * invalid. Do not touch borrowed input or clear the caller's variable.
	 * Use the same cleanup primitive for partial decode rollback; it must
	 * tolerate each explicitly tracked construction state. Callers provide
	 * synchronization with outstanding reads and encodes.
	 */
	if (!owner)
		return;
	cot_arena_destroy(&owner->arena);
	owner->~cot_decoded_event();
	cot_release(owner);
}

void cot_buffer_destroy(cot_buffer *buffer)
{
	/*
	 * Treat null as harmless. Release library-owned data through the
	 * matching cot_release seam, then zero both members. Empty input
	 * and repeated calls on the reset buffer are harmless. Do not infer
	 * allocation size from buffer->size or use caller-provided allocators.
	 * Require an empty or library-owned buffer.
	 * Callers synchronize mutation/destruction of the buffer and its bytes.
	 */
	if (!buffer)
		return;
	cot_release(buffer->data);
	buffer->data = nullptr;
	buffer->size = 0;
}

const char *cot_status_string(cot_status status)
{
	/*
	 * Select static immutable NUL-terminated English diagnostics for
	 * every public status; return "unknown status" for any unknown enum
	 * value. Never allocate, return null, use mutable shared storage, or
	 * consult locale/global configuration. Wording of known statuses is
	 * diagnostic and does not replace status-code based program logic.
	 */
	const cot_status known[] = {COT_STATUS_OK, COT_STATUS_INVALID_ARGUMENT,
		COT_STATUS_MALFORMED_MESSAGE, COT_STATUS_UNSUPPORTED_CONTENT,
		COT_STATUS_ALLOCATION_FAILURE};
	const char *const messages[] = {"success", "invalid argument",
		"malformed message", "unsupported content",
		"allocation failure"};

	for (unsigned i = 0; i < 5; ++i)
		if (!memcmp(&status, &known[i], sizeof(status)))
			return messages[i];
	return "unknown status";
}

/* Private counterpart to the Expat target's diagnostic-environment binding. */
extern "C" char *cot_expat_getenv(const char *) noexcept
{
	return nullptr;
}

cot_status cot_encode(cot_format format, const cot_event *event,
	cot_buffer *out)
{
	const cot_format xml = COT_FORMAT_XML;
	const cot_format protobuf = COT_FORMAT_PROTOBUF;

	if (!memcmp(&format, &xml, sizeof(format)))
		return cot_encode_ex(COT_FORMAT_XML, event, out, nullptr);
	if (!memcmp(&format, &protobuf, sizeof(format)))
		return cot_encode_ex(COT_FORMAT_PROTOBUF, event, out, nullptr);
	return COT_STATUS_INVALID_ARGUMENT;
}

cot_status cot_decode(cot_format format, const uint8_t *data, size_t size,
	cot_decoded_event **out)
{
	const cot_format xml = COT_FORMAT_XML;
	const cot_format protobuf = COT_FORMAT_PROTOBUF;

	if (!memcmp(&format, &xml, sizeof(format)))
		return cot_decode_ex(COT_FORMAT_XML, data, size, out, nullptr);
	if (!memcmp(&format, &protobuf, sizeof(format)))
		return cot_decode_ex(COT_FORMAT_PROTOBUF, data, size, out, nullptr);
	return COT_STATUS_INVALID_ARGUMENT;
}
