#include "internal.h"

#include <float.h>
#include <cstring>
#include <new>

#if FLT_RADIX != 2 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
#error "The official Protobuf double representation requires IEEE binary64"
#endif
static_assert(sizeof(double) == 8, "Protobuf requires 64-bit doubles");

namespace
{

enum message_kind {
	envelope,
	control,
	event_message,
	detail_message,
	contact_message,
	group_message,
	precision_message,
	status_message,
	takv_message,
	track_message,
	extension_message
};

enum scalar_kind { text_field, uint64_field, uint32_field, double_field };

struct member {
	unsigned tag;
	scalar_kind kind;
	size_t offset;
};

#define FIELD(type, name, tag, kind) {tag, kind, offsetof(type, name)}
#define POINT(name, tag)                                                       \
	{tag, double_field,                                                    \
		offsetof(cot_event, point) + offsetof(cot_point, name)}

const member event_members[] = {
	FIELD(cot_event, type, 1, text_field),
	FIELD(cot_event, access, 2, text_field),
	FIELD(cot_event, qos, 3, text_field),
	FIELD(cot_event, opex, 4, text_field),
	FIELD(cot_event, uid, 5, text_field),
	FIELD(cot_event, send_time_ms, 6, uint64_field),
	FIELD(cot_event, start_time_ms, 7, uint64_field),
	FIELD(cot_event, stale_time_ms, 8, uint64_field),
	FIELD(cot_event, how, 9, text_field),
	POINT(latitude, 10),
	POINT(longitude, 11),
	POINT(hae, 12),
	POINT(ce, 13),
	POINT(le, 14),
	FIELD(cot_event, caveat, 16, text_field),
	FIELD(cot_event, releasable_to, 17, text_field),
};
const member detail_members[] = {FIELD(cot_detail, xml, 1, text_field)};
const member contact_members[] = {
	FIELD(cot_contact, endpoint, 1, text_field),
	FIELD(cot_contact, callsign, 2, text_field),
	FIELD(cot_contact, altendpoints, 3, text_field),
};
const member group_members[] = {
	FIELD(cot_group, name, 1, text_field),
	FIELD(cot_group, role, 2, text_field),
};
const member precision_members[] = {
	FIELD(cot_precision_location, geopointsrc, 1, text_field),
	FIELD(cot_precision_location, altsrc, 2, text_field),
};
const member status_members[] = {
	FIELD(cot_status_detail, battery, 1, uint32_field),
};
const member takv_members[] = {
	FIELD(cot_takv, device, 1, text_field),
	FIELD(cot_takv, platform, 2, text_field),
	FIELD(cot_takv, os, 3, text_field),
	FIELD(cot_takv, version, 4, text_field),
};
const member track_members[] = {
	FIELD(cot_track, speed, 1, double_field),
	FIELD(cot_track, course, 2, double_field),
};
const member extension_members[] = {
	FIELD(cot_extension, id, 1, uint32_field),
	/* Bytes share the pointer/count layout but are not UTF-8 text. */
	FIELD(cot_extension, data, 2, text_field),
};

#undef FIELD
#undef POINT

static const member *members(message_kind kind, size_t *count)
{
#define MEMBERS(name)                                                          \
	*count = sizeof(name##_members) / sizeof(member);                      \
	return name##_members
	switch (kind) {
	case event_message:
		MEMBERS(event);
	case detail_message:
		MEMBERS(detail);
	case contact_message:
		MEMBERS(contact);
	case group_message:
		MEMBERS(group);
	case precision_message:
		MEMBERS(precision);
	case status_message:
		MEMBERS(status);
	case takv_message:
		MEMBERS(takv);
	case track_message:
		MEMBERS(track);
	case extension_message:
		MEMBERS(extension);
	default:
		*count = 0;
		return nullptr;
	}
#undef MEMBERS
}

struct cursor {
	const uint8_t *data;
	size_t size;
	size_t position;
};

static bool read_varint(cursor *input, uint64_t *value)
{
	uint64_t result = 0;

	for (unsigned shift = 0; shift < 70; shift += 7) {
		if (input->position == input->size)
			return false;
		uint8_t byte = input->data[input->position++];

		if (shift == 63 && byte > 1)
			return false;
		result |= uint64_t{byte & 127u} << shift;
		if (!(byte & 128)) {
			*value = result;
			return true;
		}
	}
	return false;
}

static bool read_tag(cursor *input, unsigned *field, unsigned *wire)
{
	uint64_t tag;

	if (!read_varint(input, &tag) || tag > UINT32_MAX || (tag >> 3) == 0)
		return false;
	*field = static_cast<unsigned>(tag >> 3);
	*wire = static_cast<unsigned>(tag & 7);
	return *wire <= 5;
}

static bool read_span(cursor *input, cursor *span)
{
	uint64_t size;

	if (!read_varint(input, &size) || size > input->size - input->position)
		return false;
	*span = {input->data + input->position, static_cast<size_t>(size), 0};
	input->position += static_cast<size_t>(size);
	return true;
}

struct group_frame {
	unsigned tag;
	group_frame *parent;
};

static bool skip_value(cursor *input, unsigned wire)
{
	uint64_t ignored;
	cursor span{};
	size_t size = 0;

	switch (wire) {
	case 0:
		return read_varint(input, &ignored);
	case 1:
		size = 8;
		break;
	case 2:
		return read_span(input, &span);
	case 5:
		size = 4;
		break;
	default:
		return false;
	}
	if (size > input->size - input->position)
		return false;
	input->position += size;
	return true;
}

/* Unknown groups use an explicit stack rather than unbounded C recursion. */
static bool skip_field(
	cot_arena *arena, cursor *input, unsigned tag, unsigned wire)
{
	if (wire != 3)
		return skip_value(input, wire);
	group_frame first{tag, nullptr};
	group_frame *top = &first;

	while (input->position < input->size) {
		if (!read_tag(input, &tag, &wire))
			return false;
		if (wire == 4) {
			if (tag != top->tag)
				return false;
			top = top->parent;
			if (!top)
				return true;
		} else if (wire == 3) {
			void *storage =
				cot_arena_allocate(arena, sizeof(group_frame));

			if (!storage)
				return false;
			top = new (storage) group_frame{tag, top};
		} else if (!skip_value(input, wire)) {
			return false;
		}
	}
	return false;
}

struct parse_state {
	cot_decoded_event *owner;
	const uint8_t *data;
	bool has_event;
	bool has_control;
};

static const char *message_path(message_kind kind)
{
	const char *const paths[] = {"envelope", "control", "event", "detail",
		"detail.contact", "detail.group", "detail.precision_location",
		"detail.status", "detail.takv", "detail.track",
		"detail.extensions"};

	return paths[kind];
}

static const char *field_path(message_kind kind, unsigned tag)
{
	const char *const event[] = {nullptr, "type", "access", "qos", "opex",
		"uid", "send_time_ms", "start_time_ms", "stale_time_ms", "how",
		"point.latitude", "point.longitude", "point.hae", "point.ce",
		"point.le", "detail", "caveat", "releasable_to"};
	const char *const contact[] = {nullptr, "detail.contact.endpoint",
		"detail.contact.callsign", "detail.contact.altendpoints"};
	const char *const group[] = {
		nullptr, "detail.group.name", "detail.group.role"};
	const char *const precision[] = {nullptr,
		"detail.precision_location.geopointsrc",
		"detail.precision_location.altsrc"};
	const char *const takv[] = {nullptr, "detail.takv.device",
		"detail.takv.platform", "detail.takv.os", "detail.takv.version"};
	const char *const track[] = {
		nullptr, "detail.track.speed", "detail.track.course"};

#define PATHS(name)                                                           \
	if (tag && tag < sizeof(name) / sizeof(name[0]))                       \
		return name[tag]
	switch (kind) {
	case event_message:
		PATHS(event);
		break;
	case contact_message:
		PATHS(contact);
		break;
	case group_message:
		PATHS(group);
		break;
	case precision_message:
		PATHS(precision);
		break;
	case takv_message:
		PATHS(takv);
		break;
	case track_message:
		PATHS(track);
		break;
	case detail_message:
		if (tag == 1)
			return "detail.xml";
		break;
	case status_message:
		if (tag == 1)
			return "detail.status.battery";
		break;
	default:
		break;
	}
#undef PATHS
	return message_path(kind);
}

static bool malformed(parse_state *state, message_kind kind, unsigned tag,
	const uint8_t *position)
{
	if (state->owner->arena.status == COT_STATUS_OK)
		cot_diagnostic_error(state->owner->diagnostic,
			COT_DIAGNOSTIC_MALFORMED_PROTOBUF, field_path(kind, tag),
			static_cast<size_t>(position - state->data));
	return false;
}

static void *typed_object(cot_decoded_event *owner, unsigned tag)
{
	cot_detail *detail = &owner->detail;

	switch (tag) {
	case 2:
		detail->contact = &owner->contact;
		return &owner->contact;
	case 3:
		detail->group = &owner->group;
		return &owner->group;
	case 4:
		detail->precision_location = &owner->precision;
		return &owner->precision;
	case 5:
		detail->status = &owner->status;
		return &owner->status;
	case 6:
		detail->takv = &owner->takv;
		return &owner->takv;
	case 7:
		detail->track = &owner->track;
		return &owner->track;
	default:
		return nullptr;
	}
}

static bool parse_message(
	parse_state *state, message_kind kind, void *object, cursor input);

static bool parse_nested(
	parse_state *state, message_kind kind, unsigned tag, cursor input)
{
	cot_decoded_event *owner = state->owner;

	if (kind == envelope) {
		if (tag == 1) {
			state->has_control = true;
			return parse_message(state, control, nullptr, input);
		}
		state->has_event = true;
		return parse_message(
			state, event_message, &owner->event, input);
	}
	if (kind == event_message) {
		owner->event.detail = &owner->detail;
		return parse_message(
			state, detail_message, &owner->detail, input);
	}
	if (tag < 8) {
		message_kind nested =
			static_cast<message_kind>(contact_message + tag - 2);

		return parse_message(
			state, nested, typed_object(owner, tag), input);
	}
	void *storage =
		cot_arena_allocate(&owner->arena, sizeof(cot_extension_node));

	if (!storage || owner->detail.extension_count == SIZE_MAX)
		return false;
	cot_extension_node *node = new (storage) cot_extension_node{};

	if (owner->last_extension)
		owner->last_extension->next = node;
	else
		owner->extensions = node;
	owner->last_extension = node;
	++owner->detail.extension_count;
	return parse_message(state, extension_message, &node->value, input);
}

static bool parse_scalar(
	cursor *input, const member *field, void *object, message_kind kind)
{
	uint8_t *target = static_cast<uint8_t *>(object) + field->offset;

	if (field->kind == text_field) {
		cursor span{};

		if (!read_span(input, &span))
			return false;
		cot_text text{
			reinterpret_cast<const char *>(span.data), span.size};

		if (kind != extension_message && !cot_utf8(text, false))
			return false;
		if (kind == extension_message) {
			cot_bytes bytes{span.data, span.size};

			memcpy(target, &bytes, sizeof(bytes));
		} else {
			memcpy(target, &text, sizeof(text));
		}
		return true;
	}
	if (field->kind == double_field) {
		if (input->size - input->position < 8)
			return false;
		uint64_t bits = 0;

		for (unsigned i = 0; i < 8; ++i)
			bits |= uint64_t{input->data[input->position++]}
				<< (8 * i);
		memcpy(target, &bits, sizeof(bits));
		return true;
	}
	uint64_t value;

	if (!read_varint(input, &value))
		return false;
	if (field->kind == uint32_field) {
		uint32_t small = static_cast<uint32_t>(value);

		memcpy(target, &small, sizeof(small));
	} else {
		memcpy(target, &value, sizeof(value));
	}
	return true;
}

static bool parse_message(
	parse_state *state, message_kind kind, void *object, cursor input)
{
	size_t count;
	const member *fields = members(kind, &count);

	while (input.position < input.size) {
		unsigned tag = 0, wire = 0;
		const uint8_t *position = input.data + input.position;

		if (!read_tag(&input, &tag, &wire))
			return malformed(state, kind, tag, position);
		bool nested = wire == 2 &&
			      ((kind == envelope && (tag == 1 || tag == 2)) ||
				      (kind == event_message && tag == 15) ||
				      (kind == detail_message && tag >= 2 &&
					      tag <= 8));

		if (nested) {
			cursor span{};

			if (!read_span(&input, &span) ||
				!parse_nested(state, kind, tag, span))
				return malformed(state, kind, tag, position);
			continue;
		}
		if (kind == control && tag == 3 && wire == 2) {
			cursor span{};

			if (!read_span(&input, &span) ||
				!cot_utf8({reinterpret_cast<const char *>(
						   span.data),
						  span.size},
					false))
				return malformed(state, kind, tag, position);
			continue;
		}
		if (kind == control && tag == 4 && wire == 2) {
			cursor packed{};
			uint64_t ignored;

			if (!read_span(&input, &packed))
				return malformed(state, kind, tag, position);
			while (packed.position < packed.size)
				if (!read_varint(&packed, &ignored))
					return malformed(state, kind, tag, position);
			continue;
		}
		const member *field = nullptr;

		for (size_t i = 0; i < count; ++i) {
			unsigned expected =
				fields[i].kind == text_field
					? 2
					: (fields[i].kind == double_field ? 1
									  : 0);

			if (fields[i].tag == tag && expected == wire) {
				field = &fields[i];
				break;
			}
		}
		if (field) {
			if (!parse_scalar(&input, field, object, kind))
				return malformed(state, kind, tag, position);
		} else {
			if (!skip_field(
				    &state->owner->arena, &input, tag, wire))
				return malformed(state, kind, tag, position);
			cot_diagnostic *diagnostic = state->owner->diagnostic;

			if (diagnostic) {
				++diagnostic->unknown_protobuf_fields;
				diagnostic->unknown_protobuf_bytes +=
					input.data + input.position - position;
			}
		}
	}
	return true;
}

static void write_varint(cot_writer *writer, uint64_t value)
{
	uint8_t buffer[10];
	size_t size = 0;

	do {
		buffer[size++] = static_cast<uint8_t>(
			(value & 127) | (value >= 128 ? 128 : 0));
		value >>= 7;
	} while (value);
	cot_write(writer, buffer, size);
}

static void write_message(
	cot_writer *writer, message_kind kind, const void *object);

static void write_nested(
	cot_writer *writer, unsigned tag, message_kind kind, const void *object)
{
	if (!object)
		return;
	cot_writer count{};

	count.counting = true;
	write_message(&count, kind, object);
	if (count.status != COT_STATUS_OK) {
		writer->status = count.status;
		return;
	}
	write_varint(writer, (uint64_t{tag} << 3) | 2);
	write_varint(writer, count.size);
	write_message(writer, kind, object);
}

static void write_message(
	cot_writer *writer, message_kind kind, const void *object)
{
	size_t count;
	const member *fields = members(kind, &count);
	const uint8_t *source = static_cast<const uint8_t *>(object);

	for (size_t i = 0; i < count; ++i) {
		const member *field = &fields[i];
		const void *value = source + field->offset;

		if (field->kind == text_field) {
			const void *data;
			size_t size;

			if (kind == extension_message) {
				const cot_bytes *bytes =
					static_cast<const cot_bytes *>(value);

				data = bytes->data;
				size = bytes->size;
			} else {
				const cot_text *text =
					static_cast<const cot_text *>(value);

				data = text->data;
				size = text->size;
				if (kind == event_message && field->tag == 2 &&
					cot_text_equal(*text, "Undefined"))
					size = 0;
			}
			if (size) {
				write_varint(writer,
					(uint64_t{field->tag} << 3) | 2);
				write_varint(writer, size);
				cot_write(writer, data, size);
			}
		} else {
			uint64_t bits = 0;

			if (field->kind == uint32_field) {
				uint32_t number;

				memcpy(&number, value, sizeof(number));
				bits = number;
			} else {
				memcpy(&bits, value, sizeof(bits));
			}
			if (field->kind == double_field &&
				(bits & 0x7fffffffffffffffULL) == 0)
				continue;
			if (!bits)
				continue;
			write_varint(writer,
				(uint64_t{field->tag} << 3) |
					(field->kind == double_field ? 1 : 0));
			if (field->kind == double_field) {
				uint8_t bytes[8];

				for (unsigned j = 0; j < 8; ++j)
					bytes[j] = static_cast<uint8_t>(
						bits >> (8 * j));
				cot_write(writer, bytes, sizeof(bytes));
			} else {
				write_varint(writer, bits);
			}
		}
	}
	if (kind == event_message) {
		const cot_event *event = static_cast<const cot_event *>(object);

		write_nested(writer, 15, detail_message, event->detail);
	} else if (kind == detail_message) {
		const cot_detail *detail =
			static_cast<const cot_detail *>(object);

		write_nested(writer, 2, contact_message, detail->contact);
		write_nested(writer, 3, group_message, detail->group);
		write_nested(writer, 4, precision_message,
			detail->precision_location);
		write_nested(writer, 5, status_message, detail->status);
		write_nested(writer, 6, takv_message, detail->takv);
		write_nested(writer, 7, track_message, detail->track);
		for (size_t i = 0; i < detail->extension_count; ++i)
			write_nested(writer, 8, extension_message,
				&detail->extensions[i]);
	}
}

} // namespace

cot_status cot_parse_protobuf(
	cot_decoded_event *owner, const uint8_t *data, size_t size)
{
	cot_text storage = cot_copy_text(
		&owner->arena, reinterpret_cast<const char *>(data), size);

	if (owner->arena.status != COT_STATUS_OK)
		return owner->arena.status;
	parse_state state{owner,
		reinterpret_cast<const uint8_t *>(storage.data), false, false};
	cursor input{reinterpret_cast<const uint8_t *>(storage.data), size, 0};

	if (!parse_message(&state, envelope, nullptr, input))
		return owner->arena.status == COT_STATUS_OK
			       ? COT_STATUS_MALFORMED_MESSAGE
			       : owner->arena.status;
	if (!state.has_event || state.has_control) {
		cot_diagnostic_error(owner->diagnostic,
			state.has_control ? COT_DIAGNOSTIC_UNSUPPORTED_CONTENT
					  : COT_DIAGNOSTIC_MISSING_REQUIRED,
			state.has_control ? "control" : "event",
			COT_DIAGNOSTIC_NO_OFFSET);
		return COT_STATUS_UNSUPPORTED_CONTENT;
	}
	size_t count = owner->detail.extension_count;

	if (count) {
		if (count > PTRDIFF_MAX / sizeof(cot_extension))
			return COT_STATUS_ALLOCATION_FAILURE;
		void *storage = cot_arena_allocate(
			&owner->arena, count * sizeof(cot_extension));

		if (!storage)
			return owner->arena.status;
		cot_extension *extensions = new (storage) cot_extension[count];
		cot_extension_node *node = owner->extensions;

		for (size_t i = 0; i < count; ++i, node = node->next)
			extensions[i] = node->value;
		owner->detail.extensions = extensions;
	}
	return COT_STATUS_OK;
}

void cot_write_protobuf(cot_writer *writer, const cot_event *event)
{
	write_nested(writer, 2, event_message, event);
}
