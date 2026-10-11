#include "internal.h"

#include <expat.h>
#include <climits>
#include <cmath>
#include <cstring>
#include <new>

namespace
{

enum node_kind { element_node, text_node, comment_node, instruction_node };

struct xml_attribute {
	cot_text name;
	cot_text uri;
	cot_text prefix;
	cot_text value;
	bool declaration;
	xml_attribute *next;
};

struct xml_node {
	node_kind kind;
	cot_text name;
	cot_text uri;
	cot_text prefix;
	cot_text value;
	xml_attribute *attributes;
	xml_node *parent;
	xml_node *first;
	xml_node *last;
	xml_node *next;
	bool omitted;
	size_t byte_offset;
};

struct xml_state {
	cot_arena *arena;
	XML_Parser parser;
	xml_node *root;
	xml_node *current;
	xml_attribute *pending_namespaces;
	cot_status status;
};

/* Expat's realloc callback needs a size, without any global allocator state. */
struct alignas(max_align_t) parser_allocation {
	size_t size;
};

static void *parser_allocate(size_t size)
{
	if (size > SIZE_MAX - sizeof(parser_allocation))
		return nullptr;
	void *storage = cot_allocate(sizeof(parser_allocation) + size);

	if (!storage)
		return nullptr;
	auto *header = new (storage) parser_allocation{size};

	return header + 1;
}

static void parser_release(void *allocation)
{
	if (allocation)
		cot_release(static_cast<parser_allocation *>(allocation) - 1);
}

static void *parser_reallocate(void *allocation, size_t size)
{
	if (!allocation)
		return parser_allocate(size);
	if (!size) {
		parser_release(allocation);
		return nullptr;
	}
	auto *old = static_cast<parser_allocation *>(allocation) - 1;
	void *copy = parser_allocate(size);

	if (!copy)
		return nullptr;
	memcpy(copy, allocation, old->size < size ? old->size : size);
	parser_release(allocation);
	return copy;
}

static cot_text copy_string(cot_arena *arena, const char *value)
{
	return value ? cot_copy_text(arena, value, strlen(value)) : cot_text{};
}

static void split_name(cot_arena *arena, const char *expanded, cot_text *name,
	cot_text *uri, cot_text *prefix)
{
	const char *separator = strchr(expanded, '\x1f');

	if (!separator) {
		*name = copy_string(arena, expanded);
		return;
	}
	*uri = cot_copy_text(arena, expanded, separator - expanded);
	const char *local = separator + 1;
	const char *end = strchr(local, '\x1f');

	*name = cot_copy_text(arena, local, end ? end - local : strlen(local));
	if (end)
		*prefix = copy_string(arena, end + 1);
}

static void stop(xml_state *state, cot_status status)
{
	if (state->status == COT_STATUS_OK)
		state->status = status;
	XML_StopParser(state->parser, XML_FALSE);
}

static xml_node *new_node(xml_state *state, node_kind kind)
{
	void *storage = cot_arena_allocate(state->arena, sizeof(xml_node));

	if (!storage) {
		stop(state, COT_STATUS_ALLOCATION_FAILURE);
		return nullptr;
	}
	xml_node *node = new (storage) xml_node{};

	node->kind = kind;
	int64_t offset = XML_GetCurrentByteIndex64(state->parser);

	node->byte_offset = offset < 0 ? COT_DIAGNOSTIC_NO_OFFSET
				      : static_cast<size_t>(offset);
	node->parent = state->current;
	if (state->current) {
		if (state->current->last)
			state->current->last->next = node;
		else
			state->current->first = node;
		state->current->last = node;
	} else {
		state->root = node;
	}
	return node;
}

static void XMLCALL start_namespace(
	void *opaque, const char *prefix, const char *uri)
{
	auto *state = static_cast<xml_state *>(opaque);

	if (state->status != COT_STATUS_OK)
		return;
	void *storage = cot_arena_allocate(state->arena, sizeof(xml_attribute));

	if (!storage) {
		stop(state, COT_STATUS_ALLOCATION_FAILURE);
		return;
	}
	xml_attribute *attribute = new (storage) xml_attribute{};

	attribute->name = copy_string(state->arena, prefix);
	attribute->value = copy_string(state->arena, uri);
	attribute->declaration = true;
	attribute->next = state->pending_namespaces;
	state->pending_namespaces = attribute;
	if (state->arena->status != COT_STATUS_OK)
		stop(state, state->arena->status);
}

static void XMLCALL start_element(
	void *opaque, const char *name, const char **attributes)
{
	auto *state = static_cast<xml_state *>(opaque);

	if (state->status != COT_STATUS_OK)
		return;
	xml_node *node = new_node(state, element_node);

	if (!node)
		return;
	split_name(state->arena, name, &node->name, &node->uri, &node->prefix);
	node->attributes = state->pending_namespaces;
	state->pending_namespaces = nullptr;
	for (size_t i = 0; attributes[i]; i += 2) {
		void *storage =
			cot_arena_allocate(state->arena, sizeof(xml_attribute));

		if (!storage)
			break;
		xml_attribute *attribute = new (storage) xml_attribute{};

		split_name(state->arena, attributes[i], &attribute->name,
			&attribute->uri, &attribute->prefix);
		attribute->value = copy_string(state->arena, attributes[i + 1]);
		attribute->next = node->attributes;
		node->attributes = attribute;
	}
	state->current = node;
	if (state->arena->status != COT_STATUS_OK)
		stop(state, state->arena->status);
}

static void XMLCALL end_element(void *opaque, const char *)
{
	auto *state = static_cast<xml_state *>(opaque);

	if (state->status == COT_STATUS_OK && state->current)
		state->current = state->current->parent;
}

static void XMLCALL character_data(void *opaque, const char *text, int size)
{
	auto *state = static_cast<xml_state *>(opaque);

	if (state->status != COT_STATUS_OK || !state->current || !size)
		return;
	xml_node *node = new_node(state, text_node);

	if (!node)
		return;
	node->value =
		cot_copy_text(state->arena, text, static_cast<size_t>(size));
	if (state->arena->status != COT_STATUS_OK)
		stop(state, state->arena->status);
}

static void XMLCALL comment(void *opaque, const char *text)
{
	auto *state = static_cast<xml_state *>(opaque);

	if (state->status != COT_STATUS_OK)
		return;
	if (!state->current) {
		if (state->root)
			stop(state, COT_STATUS_MALFORMED_MESSAGE);
		return;
	}
	xml_node *node = new_node(state, comment_node);

	if (!node)
		return;
	node->value = copy_string(state->arena, text);
	if (state->arena->status != COT_STATUS_OK)
		stop(state, state->arena->status);
}

static void XMLCALL instruction(
	void *opaque, const char *target, const char *data)
{
	auto *state = static_cast<xml_state *>(opaque);

	if (state->status != COT_STATUS_OK)
		return;
	if (!state->current) {
		if (state->root)
			stop(state, COT_STATUS_MALFORMED_MESSAGE);
		return;
	}
	xml_node *node = new_node(state, instruction_node);

	if (!node)
		return;
	node->name = copy_string(state->arena, target);
	node->value = copy_string(state->arena, data);
	if (state->arena->status != COT_STATUS_OK)
		stop(state, state->arena->status);
}

static void XMLCALL doctype(
	void *opaque, const char *, const char *, const char *, int)
{
	stop(static_cast<xml_state *>(opaque), COT_STATUS_UNSUPPORTED_CONTENT);
}

static void XMLCALL declaration(
	void *opaque, const char *version, const char *encoding, int)
{
	auto *state = static_cast<xml_state *>(opaque);
	bool utf8 = !encoding ||
		    (strlen(encoding) == 5 &&
			    (encoding[0] == 'U' || encoding[0] == 'u') &&
			    (encoding[1] == 'T' || encoding[1] == 't') &&
			    (encoding[2] == 'F' || encoding[2] == 'f') &&
			    encoding[3] == '-' && encoding[4] == '8');

	if (!version || strcmp(version, "1.0") || !utf8)
		stop(state, COT_STATUS_UNSUPPORTED_CONTENT);
}

static cot_status parse_document(
	cot_arena *arena, cot_text input, bool fragment, xml_node **root,
	cot_diagnostic *diagnostic)
{
	if (!cot_utf8(input, false))
		return COT_STATUS_MALFORMED_MESSAGE;
	const XML_Memory_Handling_Suite memory = {
		parser_allocate, parser_reallocate, parser_release};
	const char separator = '\x1f';
	xml_state state{};

	state.arena = arena;
	state.parser = XML_ParserCreate_MM("UTF-8", &memory, &separator);
	if (!state.parser)
		return COT_STATUS_ALLOCATION_FAILURE;
	/* Per-operation salt avoids Expat's implicit entropy-source I/O. */
	uintptr_t a = reinterpret_cast<uintptr_t>(&state);
	uintptr_t b = reinterpret_cast<uintptr_t>(state.parser);
	uint8_t salt[16];

	for (unsigned i = 0; i < sizeof(salt); ++i) {
		salt[i] = static_cast<uint8_t>(
			(a >> ((i % sizeof(a)) * 8)) ^
			(b >> (((i + 3) % sizeof(b)) * 8)) ^ (i * 37 + 1));
	}
	XML_SetHashSalt16Bytes(state.parser, salt);
	XML_SetUserData(state.parser, &state);
	XML_SetReturnNSTriplet(state.parser, 1);
	XML_SetNamespaceDeclHandler(state.parser, start_namespace, nullptr);
	XML_SetElementHandler(state.parser, start_element, end_element);
	XML_SetCharacterDataHandler(state.parser, character_data);
	XML_SetCommentHandler(state.parser, comment);
	XML_SetProcessingInstructionHandler(state.parser, instruction);
	XML_SetStartDoctypeDeclHandler(state.parser, doctype);
	XML_SetXmlDeclHandler(state.parser, declaration);
	const cot_text pieces[] = {
		{fragment ? "<detail>" : "", fragment ? 8u : 0u}, input,
		{fragment ? "</detail>" : "", fragment ? 9u : 0u}};
	bool good = true;

	for (unsigned part = 0; part < 3 && good; ++part) {
		size_t position = 0;

		do {
			size_t remaining = pieces[part].size - position;
			int size = remaining > INT_MAX
					   ? INT_MAX
					   : static_cast<int>(remaining);
			const char *data =
				size ? pieces[part].data + position : "";
			bool final = part == 2 && remaining <= INT_MAX;

			good = XML_Parse(state.parser, data, size, final) ==
			       XML_STATUS_OK;
			position += static_cast<size_t>(size);
		} while (position < pieces[part].size && good);
	}
	cot_status status = state.status;

	if (!good && status == COT_STATUS_OK)
		status = XML_GetErrorCode(state.parser) == XML_ERROR_NO_MEMORY
				 ? COT_STATUS_ALLOCATION_FAILURE
				 : COT_STATUS_MALFORMED_MESSAGE;
	if (status != COT_STATUS_OK) {
		int64_t offset = XML_GetCurrentByteIndex64(state.parser);
		cot_diagnostic_reason reason = COT_DIAGNOSTIC_MALFORMED_XML;

		if (status == COT_STATUS_ALLOCATION_FAILURE)
			reason = COT_DIAGNOSTIC_ALLOCATION_FAILURE;
		else if (status == COT_STATUS_UNSUPPORTED_CONTENT)
			reason = COT_DIAGNOSTIC_UNSUPPORTED_CONTENT;
		cot_diagnostic_error(diagnostic, reason, nullptr,
			offset < 0 ? COT_DIAGNOSTIC_NO_OFFSET
				   : static_cast<size_t>(offset));
	}
	XML_ParserFree(state.parser);
	if (status == COT_STATUS_OK)
		*root = state.root;
	return status;
}

static bool whitespace(cot_text value)
{
	for (size_t i = 0; i < value.size; ++i)
		if (value.data[i] != ' ' && value.data[i] != '\n' &&
			value.data[i] != '\r' && value.data[i] != '\t')
			return false;
	return true;
}

static xml_attribute *attribute(xml_node *node, const char *name)
{
	for (xml_attribute *a = node->attributes; a; a = a->next)
		if (!a->declaration && !a->uri.size &&
			cot_text_equal(a->name, name))
			return a;
	return nullptr;
}

static bool named(const xml_node *node, const char *name)
{
	return node->kind == element_node && !node->uri.size &&
	       cot_text_equal(node->name, name);
}

static bool allowed_attributes(
	xml_node *node, const char *const *names, size_t count)
{
	for (xml_attribute *a = node->attributes; a; a = a->next) {
		if (a->declaration)
			continue;
		bool found = false;

		for (size_t i = 0; i < count; ++i)
			if (!a->uri.size && cot_text_equal(a->name, names[i]))
				found = true;
		if (!found)
			return false;
	}
	return true;
}

static void escaped(cot_writer *writer, cot_text text, bool attr)
{
	if (!cot_utf8(text, true)) {
		writer->status = COT_STATUS_UNSUPPORTED_CONTENT;
		return;
	}
	size_t start = 0;

	for (size_t i = 0; i < text.size; ++i) {
		const char *replacement = nullptr;

		switch (text.data[i]) {
		case '&':
			replacement = "&amp;";
			break;
		case '<':
			replacement = "&lt;";
			break;
		case '>':
			replacement = "&gt;";
			break;
		case '"':
			if (attr)
				replacement = "&quot;";
			break;
		case '\r':
			replacement = "&#13;";
			break;
		case '\n':
			if (attr)
				replacement = "&#10;";
			break;
		case '\t':
			if (attr)
				replacement = "&#9;";
			break;
		}
		if (!replacement)
			continue;
		cot_write(writer, text.data + start, i - start);
		cot_write_literal(writer, replacement);
		start = i + 1;
	}
	if (start < text.size)
		cot_write(writer, text.data + start, text.size - start);
}

static void qname(cot_writer *writer, cot_text prefix, cot_text name)
{
	if (prefix.size) {
		cot_write(writer, prefix.data, prefix.size);
		cot_write_literal(writer, ":");
	}
	cot_write(writer, name.data, name.size);
}

static bool equal(cot_text a, cot_text b)
{
	return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}

static void emit_namespace(cot_writer *writer, cot_text prefix, cot_text uri)
{
	cot_write_literal(writer, " xmlns");
	if (prefix.size) {
		cot_write_literal(writer, ":");
		cot_write(writer, prefix.data, prefix.size);
	}
	cot_write_literal(writer, "=\"");
	escaped(writer, uri, true);
	cot_write_literal(writer, "\"");
}

static void open_element(
	cot_writer *writer, const xml_node *node, bool detached)
{
	cot_write_literal(writer, "<");
	qname(writer, node->prefix, node->name);
	/* Preserve bindings used only in QName-valued text as well. */
	if (detached) {
		for (const xml_node *scope = node->parent; scope;
			scope = scope->parent) {
			for (xml_attribute *a = scope->attributes; a;
				a = a->next) {
				if (!a->declaration)
					continue;
				bool shadowed = false;

				for (const xml_node *nearer = node;
					nearer != scope;
					nearer = nearer->parent) {
					for (xml_attribute *p =
							nearer->attributes;
						p; p = p->next)
						if (p->declaration &&
							equal(p->name, a->name))
							shadowed = true;
				}
				if (!shadowed)
					emit_namespace(
						writer, a->name, a->value);
			}
		}
	}
	for (xml_attribute *a = node->attributes; a; a = a->next) {
		if (a->declaration) {
			emit_namespace(writer, a->name, a->value);
			continue;
		}
		cot_write_literal(writer, " ");
		qname(writer, a->prefix, a->name);
		cot_write_literal(writer, "=\"");
		escaped(writer, a->value, true);
		cot_write_literal(writer, "\"");
	}
	cot_write_literal(writer, ">");
}

/* Traverse residual trees iteratively using parent/sibling links. */
static void serialize(cot_writer *writer, xml_node *start)
{
	xml_node *node = start;
	bool entering = true;

	while (writer->status == COT_STATUS_OK) {
		if (entering && !node->omitted) {
			switch (node->kind) {
			case element_node:
				open_element(writer, node, node == start);
				if (node->first) {
					node = node->first;
					continue;
				}
				break;
			case text_node:
				escaped(writer, node->value, false);
				break;
			case comment_node:
				cot_write_literal(writer, "<!--");
				cot_write(writer, node->value.data,
					node->value.size);
				cot_write_literal(writer, "-->");
				break;
			case instruction_node:
				cot_write_literal(writer, "<?");
				cot_write(writer, node->name.data,
					node->name.size);
				if (node->value.size) {
					cot_write_literal(writer, " ");
					cot_write(writer, node->value.data,
						node->value.size);
				}
				cot_write_literal(writer, "?>");
				break;
			}
		}
		if (!node->omitted && node->kind == element_node) {
			cot_write_literal(writer, "</");
			qname(writer, node->prefix, node->name);
			cot_write_literal(writer, ">");
		}
		if (node == start)
			break;
		if (node->next) {
			node = node->next;
			entering = true;
		} else {
			node = node->parent;
			entering = false;
		}
	}
}

const char *const detail_names[] = {
	"contact", "__group", "precisionlocation", "status", "takv", "track"};

static bool map_detail(cot_decoded_event *owner, xml_node *node, unsigned kind)
{
	const char *names[4]{};
	unsigned count = 0;
	unsigned required = 0;

	switch (kind) {
	case 0:
		names[0] = "callsign";
		names[1] = "endpoint";
		names[2] = "altendpoints";
		count = 3;
		required = 1;
		break;
	case 1:
		names[0] = "name";
		names[1] = "role";
		count = required = 2;
		break;
	case 2:
		names[0] = "geopointsrc";
		names[1] = "altsrc";
		count = required = 2;
		break;
	case 3:
		names[0] = "battery";
		count = required = 1;
		break;
	case 4:
		names[0] = "device";
		names[1] = "platform";
		names[2] = "os";
		names[3] = "version";
		count = required = 4;
		break;
	case 5:
		names[0] = "speed";
		names[1] = "course";
		count = required = 2;
		break;
	}
	if (!allowed_attributes(node, names, count))
		return false;
	for (xml_node *child = node->first; child; child = child->next)
		if (child->kind != text_node || !whitespace(child->value))
			return false;
	cot_text values[4]{};

	for (unsigned i = 0; i < count; ++i) {
		xml_attribute *a = attribute(node, names[i]);

		if (!a && i < required)
			return false;
		if (a)
			values[i] = a->value;
	}
	cot_detail *detail = &owner->detail;
	uint64_t battery;
	double speed, course;

	switch (kind) {
	case 0:
		owner->contact = {values[1], values[0], values[2]};
		detail->contact = &owner->contact;
		break;
	case 1:
		owner->group = {values[0], values[1]};
		detail->group = &owner->group;
		break;
	case 2:
		owner->precision = {values[0], values[1]};
		detail->precision_location = &owner->precision;
		break;
	case 3:
		if (!cot_parse_uint(values[0], UINT32_MAX, &battery))
			return false;
		owner->status.battery = static_cast<uint32_t>(battery);
		detail->status = &owner->status;
		break;
	case 4:
		owner->takv = {values[0], values[1], values[2], values[3]};
		detail->takv = &owner->takv;
		break;
	case 5:
		if (!cot_parse_number(values[0], &speed) ||
			!cot_parse_number(values[1], &course) ||
			!std::isfinite(speed) || !std::isfinite(course))
			return false;
		owner->track = {speed, course};
		detail->track = &owner->track;
		break;
	}
	return true;
}

static cot_status parse_details(cot_decoded_event *owner, xml_node *node)
{
	for (xml_attribute *a = node->attributes; a; a = a->next)
		if (!a->declaration)
			return COT_STATUS_UNSUPPORTED_CONTENT;
	owner->event.detail = &owner->detail;
	for (unsigned kind = 0; kind < 6; ++kind) {
		xml_node *found = nullptr;
		size_t count = 0;

		for (xml_node *child = node->first; child;
			child = child->next) {
			if (named(child, detail_names[kind])) {
				found = child;
				++count;
			}
		}
		if (count == 1 && map_detail(owner, found, kind))
			found->omitted = true;
	}
	cot_writer residual{};

	for (xml_node *child = node->first; child; child = child->next)
		serialize(&residual, child);
	cot_status status = residual.status;

	if (status == COT_STATUS_OK) {
		owner->detail.xml = cot_copy_text(&owner->arena,
			reinterpret_cast<const char *>(residual.data),
			residual.size);
		status = owner->arena.status;
	}
	cot_writer_destroy(&residual);
	return status;
}

static cot_status event_error(cot_decoded_event *owner, cot_status status,
	cot_diagnostic_reason reason, const char *path, const xml_node *node)
{
	cot_diagnostic_error(owner->diagnostic, reason, path,
		node ? node->byte_offset : COT_DIAGNOSTIC_NO_OFFSET);
	return status;
}

static cot_status parse_event(cot_decoded_event *owner, xml_node *root)
{
	if (!root || !named(root, "event"))
		return event_error(owner, COT_STATUS_UNSUPPORTED_CONTENT,
			COT_DIAGNOSTIC_UNSUPPORTED_CONTENT, "event", root);
	const char *const names[] = {"version", "type", "uid", "time", "start",
		"stale", "how", "access", "qos", "opex", "caveat",
		"releasableTo"};
	const char *const paths[] = {"version", "type", "uid", "send_time_ms",
		"start_time_ms", "stale_time_ms", "how", "access", "qos",
		"opex", "caveat", "releasable_to"};
	cot_text values[12]{};

	for (unsigned i = 0; i < 12; ++i) {
		xml_attribute *a = attribute(root, names[i]);

		if (!a && i < 7)
			return event_error(owner, COT_STATUS_MALFORMED_MESSAGE,
				COT_DIAGNOSTIC_MISSING_REQUIRED, paths[i], root);
		if (a)
			values[i] = a->value;
		if (a && (i == 1 || i == 2 || i == 6) && !a->value.size)
			return event_error(owner, COT_STATUS_MALFORMED_MESSAGE,
				COT_DIAGNOSTIC_INVALID_VALUE, paths[i], root);
	}
	if (!cot_text_equal(values[0], "2.0"))
		return event_error(owner, COT_STATUS_UNSUPPORTED_CONTENT,
			COT_DIAGNOSTIC_UNSUPPORTED_CONTENT, "version", root);
	if (!allowed_attributes(root, names, 12))
		return event_error(owner, COT_STATUS_UNSUPPORTED_CONTENT,
			COT_DIAGNOSTIC_UNSUPPORTED_CONTENT, "event", root);
	cot_event *event = &owner->event;

	event->type = values[1];
	event->uid = values[2];
	event->how = values[6];
	event->access =
		cot_text_equal(values[7], "Undefined") ? cot_text{} : values[7];
	event->qos = values[8];
	event->opex = values[9];
	event->caveat = values[10];
	event->releasable_to = values[11];
	uint64_t *times[] = {&event->send_time_ms, &event->start_time_ms,
		&event->stale_time_ms};

	for (unsigned i = 0; i < 3; ++i) {
		cot_status status =
			cot_parse_timestamp(values[3 + i], times[i]);

		if (status != COT_STATUS_OK)
			return event_error(owner, status,
				COT_DIAGNOSTIC_INVALID_VALUE, paths[3 + i], root);
	}
	xml_node *point = nullptr;
	xml_node *detail = nullptr;

	for (xml_node *child = root->first; child; child = child->next) {
		if (named(child, "point")) {
			if (point)
				return event_error(owner,
					COT_STATUS_MALFORMED_MESSAGE,
					COT_DIAGNOSTIC_INVALID_VALUE, "point",
					child);
			point = child;
		} else if (named(child, "detail")) {
			if (detail)
				return event_error(owner,
					COT_STATUS_MALFORMED_MESSAGE,
					COT_DIAGNOSTIC_INVALID_VALUE, "detail",
					child);
			detail = child;
		} else if (child->kind == element_node ||
			   (child->kind == text_node &&
				   !whitespace(child->value))) {
			return event_error(owner, COT_STATUS_UNSUPPORTED_CONTENT,
				COT_DIAGNOSTIC_UNSUPPORTED_CONTENT, "event", child);
		}
	}
	if (!point)
		return event_error(owner, COT_STATUS_MALFORMED_MESSAGE,
			COT_DIAGNOSTIC_MISSING_REQUIRED, "point", root);
	const char *const coordinates[] = {"lat", "lon", "hae", "ce", "le"};
	const char *const point_paths[] = {"point.latitude", "point.longitude",
		"point.hae", "point.ce", "point.le"};
	double *numbers[] = {&event->point.latitude, &event->point.longitude,
		&event->point.hae, &event->point.ce, &event->point.le};

	for (unsigned i = 0; i < 5; ++i) {
		xml_attribute *a = attribute(point, coordinates[i]);

		if (!a)
			return event_error(owner, COT_STATUS_MALFORMED_MESSAGE,
				COT_DIAGNOSTIC_MISSING_REQUIRED, point_paths[i],
				point);
		if (!cot_parse_number(a->value, numbers[i]))
			return event_error(owner, COT_STATUS_MALFORMED_MESSAGE,
				COT_DIAGNOSTIC_INVALID_VALUE, point_paths[i], point);
		double value = *numbers[i];

		if (!std::isfinite(value) ||
			(i == 0 && (value < -90 || value > 90)) ||
			(i == 1 && (value < -180 || value > 180)) ||
			(i >= 3 && value < 0))
			return event_error(owner, COT_STATUS_UNSUPPORTED_CONTENT,
				COT_DIAGNOSTIC_INVALID_VALUE, point_paths[i], point);
	}
	if (point->first || !allowed_attributes(point, coordinates, 5))
		return event_error(owner, COT_STATUS_UNSUPPORTED_CONTENT,
			COT_DIAGNOSTIC_UNSUPPORTED_CONTENT, "point", point);
	cot_status status = detail ? parse_details(owner, detail) : COT_STATUS_OK;

	if (status != COT_STATUS_OK)
		return event_error(owner, status,
			status == COT_STATUS_ALLOCATION_FAILURE
				? COT_DIAGNOSTIC_ALLOCATION_FAILURE
				: COT_DIAGNOSTIC_UNSUPPORTED_CONTENT,
			"detail", detail);
	return status;
}

static void text_attribute(cot_writer *writer, const char *name, cot_text value,
	bool required = true)
{
	if (!required && !value.size)
		return;
	cot_write_literal(writer, " ");
	cot_write_literal(writer, name);
	cot_write_literal(writer, "=\"");
	escaped(writer, value, true);
	cot_write_literal(writer, "\"");
}

static void number_attribute(cot_writer *writer, const char *name, double value)
{
	cot_write_literal(writer, " ");
	cot_write_literal(writer, name);
	cot_write_literal(writer, "=\"");
	cot_write_number(writer, value);
	cot_write_literal(writer, "\"");
}

static void time_attribute(cot_writer *writer, const char *name, uint64_t value)
{
	cot_write_literal(writer, " ");
	cot_write_literal(writer, name);
	cot_write_literal(writer, "=\"");
	cot_write_timestamp(writer, value);
	cot_write_literal(writer, "\"");
}

static void write_typed_detail(
	cot_writer *writer, const cot_detail *detail, unsigned kind)
{
	const void *pointers[] = {detail->contact, detail->group,
		detail->precision_location, detail->status, detail->takv,
		detail->track};

	if (!pointers[kind])
		return;
	if (kind == 5 && (!std::isfinite(detail->track->speed) ||
				 !std::isfinite(detail->track->course))) {
		writer->status = COT_STATUS_UNSUPPORTED_CONTENT;
		return;
	}
	cot_write_literal(writer, "<");
	cot_write_literal(writer, detail_names[kind]);
	switch (kind) {
	case 0:
		text_attribute(writer, "callsign", detail->contact->callsign);
		text_attribute(
			writer, "endpoint", detail->contact->endpoint, false);
		text_attribute(writer, "altendpoints",
			detail->contact->altendpoints, false);
		break;
	case 1:
		text_attribute(writer, "name", detail->group->name);
		text_attribute(writer, "role", detail->group->role);
		break;
	case 2:
		text_attribute(writer, "geopointsrc",
			detail->precision_location->geopointsrc);
		text_attribute(
			writer, "altsrc", detail->precision_location->altsrc);
		break;
	case 3:
		cot_write_literal(writer, " battery=\"");
		cot_write_uint(writer, detail->status->battery);
		cot_write_literal(writer, "\"");
		break;
	case 4:
		text_attribute(writer, "device", detail->takv->device);
		text_attribute(writer, "platform", detail->takv->platform);
		text_attribute(writer, "os", detail->takv->os);
		text_attribute(writer, "version", detail->takv->version);
		break;
	case 5:
		number_attribute(writer, "speed", detail->track->speed);
		number_attribute(writer, "course", detail->track->course);
		break;
	}
	cot_write_literal(writer, "/>");
}

} // namespace

cot_status cot_parse_xml(
	cot_decoded_event *owner, const uint8_t *data, size_t size)
{
	xml_node *root = nullptr;
	cot_status status = parse_document(&owner->arena,
		{reinterpret_cast<const char *>(data), size}, false, &root,
		owner->diagnostic);

	return status == COT_STATUS_OK ? parse_event(owner, root) : status;
}

void cot_write_xml(cot_writer *writer, const cot_event *event)
{
	if (!event->type.size || !event->uid.size || !event->how.size ||
		!cot_xml_point(&event->point) ||
		event->send_time_ms > 253402300799999ULL ||
		event->start_time_ms > 253402300799999ULL ||
		event->stale_time_ms > 253402300799999ULL ||
		(event->detail && event->detail->extension_count)) {
		writer->status = COT_STATUS_UNSUPPORTED_CONTENT;
		return;
	}
	cot_arena arena{};
	xml_node *residual = nullptr;

	if (event->detail && event->detail->xml.size) {
		cot_status status = parse_document(
			&arena, event->detail->xml, true, &residual, nullptr);

		if (status != COT_STATUS_OK) {
			writer->status =
				status == COT_STATUS_ALLOCATION_FAILURE
					? status
					: COT_STATUS_UNSUPPORTED_CONTENT;
			cot_diagnostic_error(writer->diagnostic,
				status == COT_STATUS_ALLOCATION_FAILURE
					? COT_DIAGNOSTIC_ALLOCATION_FAILURE
					: COT_DIAGNOSTIC_UNSUPPORTED_CONTENT,
				"detail.xml", COT_DIAGNOSTIC_NO_OFFSET);
			cot_arena_destroy(&arena);
			return;
		}
	}
	cot_write_literal(writer, "<event version=\"2.0\"");
	text_attribute(writer, "type", event->type);
	text_attribute(writer, "uid", event->uid);
	text_attribute(writer, "how", event->how);
	if (!cot_text_equal(event->access, "Undefined"))
		text_attribute(writer, "access", event->access, false);
	text_attribute(writer, "qos", event->qos, false);
	text_attribute(writer, "opex", event->opex, false);
	text_attribute(writer, "caveat", event->caveat, false);
	text_attribute(writer, "releasableTo", event->releasable_to, false);
	time_attribute(writer, "time", event->send_time_ms);
	time_attribute(writer, "start", event->start_time_ms);
	time_attribute(writer, "stale", event->stale_time_ms);
	cot_write_literal(writer, "><point");
	number_attribute(writer, "lat", event->point.latitude);
	number_attribute(writer, "lon", event->point.longitude);
	number_attribute(writer, "hae", event->point.hae);
	number_attribute(writer, "ce", event->point.ce);
	number_attribute(writer, "le", event->point.le);
	cot_write_literal(writer, "/>");
	if (event->detail) {
		cot_write_literal(writer, "<detail>");
		if (residual)
			for (xml_node *child = residual->first; child;
				child = child->next)
				serialize(writer, child);
		for (unsigned kind = 0; kind < 6; ++kind) {
			bool suppressed = false;

			if (residual)
				for (xml_node *child = residual->first; child;
					child = child->next)
					if (named(child, detail_names[kind]))
						suppressed = true;
			if (!suppressed && writer->status == COT_STATUS_OK) {
				write_typed_detail(writer, event->detail, kind);
				const char *const paths[] = {"detail.contact",
					"detail.group", "detail.precision_location",
					"detail.status", "detail.takv", "detail.track"};

				if (writer->status == COT_STATUS_UNSUPPORTED_CONTENT)
					cot_diagnostic_error(writer->diagnostic,
						COT_DIAGNOSTIC_INVALID_VALUE,
						paths[kind], COT_DIAGNOSTIC_NO_OFFSET);
			}
		}
		cot_write_literal(writer, "</detail>");
	}
	cot_write_literal(writer, "</event>");
	cot_arena_destroy(&arena);
}
