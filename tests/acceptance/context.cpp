#include "context.h"
#include <expat.h>
#include <algorithm>
#include <limits>
#include <sstream>

cot_text EventContext::keep(const std::string &value)
{
	storage.push_back(value);
	return text(storage.back());
}

const cot_event *EventContext::view() const
{
	return owner.get();
}

void EventContext::encode(cot_format kind, const cot_event *value)
{
	cot_buffer_destroy(&output.value);
	encoded = cot_encode(kind, value, &output.value);
	calls.push_back(encoded);
}

void EventContext::decode(cot_format kind, const std::string &value)
{
	cot_decoded_event_destroy(owner.value);
	owner.value = nullptr;
	decoded = cot_test::decode(kind, value, owner);
	calls.push_back(decoded);
}

void EventContext::roundtrip(const std::vector<cot_format> &formats)
{
	for (cot_format kind : formats) {
		encode(kind, owner.value ? view() : &caller);
		ASSERT_EQ(encoded, COT_STATUS_OK);
		decode(kind, bytes(output.value));
		ASSERT_EQ(decoded, COT_STATUS_OK);
	}
}

cot_format format(const std::string &name)
{
	return name == "XML" ? COT_FORMAT_XML : COT_FORMAT_PROTOBUF;
}

std::string string(cot_text value)
{
	return value.size ? std::string(value.data, value.size) : "";
}

static void same_double(double a, double b)
{
	if (std::isnan(b))
		EXPECT_TRUE(std::isnan(a));
	else
		EXPECT_EQ(a, b);
}

void same_core(const cot_event *a, const cot_event *b)
{
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(string(a->type), string(b->type));
	EXPECT_EQ(string(a->uid), string(b->uid));
	EXPECT_EQ(string(a->how), string(b->how));
	EXPECT_EQ(string(a->access), string(b->access));
	EXPECT_EQ(string(a->qos), string(b->qos));
	EXPECT_EQ(string(a->opex), string(b->opex));
	EXPECT_EQ(string(a->caveat), string(b->caveat));
	EXPECT_EQ(string(a->releasable_to), string(b->releasable_to));
	EXPECT_EQ(a->send_time_ms, b->send_time_ms);
	EXPECT_EQ(a->start_time_ms, b->start_time_ms);
	EXPECT_EQ(a->stale_time_ms, b->stale_time_ms);
	same_double(a->point.latitude, b->point.latitude);
	same_double(a->point.longitude, b->point.longitude);
	same_double(a->point.hae, b->point.hae);
	same_double(a->point.ce, b->point.ce);
	same_double(a->point.le, b->point.le);
}

void same_event(const cot_event *a, const cot_event *b)
{
	same_core(a, b);
	ASSERT_EQ(a->detail == nullptr, b->detail == nullptr);
	if (!b->detail)
		return;
	const cot_detail &x = *a->detail;
	const cot_detail &y = *b->detail;

	EXPECT_EQ(string(x.xml), string(y.xml));
	for (const char *name : {"contact", "group", "precision_location",
		     "status", "takv", "track"})
		ASSERT_EQ(
			typed(&x, name) == nullptr, typed(&y, name) == nullptr);
	if (y.contact) {
		EXPECT_EQ(string(x.contact->callsign),
			string(y.contact->callsign));
		EXPECT_EQ(string(x.contact->endpoint),
			string(y.contact->endpoint));
		EXPECT_EQ(string(x.contact->altendpoints),
			string(y.contact->altendpoints));
	}
	if (y.group) {
		EXPECT_EQ(string(x.group->name), string(y.group->name));
		EXPECT_EQ(string(x.group->role), string(y.group->role));
	}
	if (y.precision_location) {
		EXPECT_EQ(string(x.precision_location->geopointsrc),
			string(y.precision_location->geopointsrc));
		EXPECT_EQ(string(x.precision_location->altsrc),
			string(y.precision_location->altsrc));
	}
	if (y.status)
		EXPECT_EQ(x.status->battery, y.status->battery);
	if (y.takv) {
		EXPECT_EQ(string(x.takv->device), string(y.takv->device));
		EXPECT_EQ(string(x.takv->platform), string(y.takv->platform));
		EXPECT_EQ(string(x.takv->os), string(y.takv->os));
		EXPECT_EQ(string(x.takv->version), string(y.takv->version));
	}
	if (y.track) {
		same_double(x.track->speed, y.track->speed);
		same_double(x.track->course, y.track->course);
	}
	ASSERT_EQ(x.extension_count, y.extension_count);
	for (size_t i = 0; i < y.extension_count; ++i) {
		EXPECT_EQ(x.extensions[i].id, y.extensions[i].id);
		const cot_bytes &p = x.extensions[i].data;
		const cot_bytes &q = y.extensions[i].data;

		EXPECT_EQ(p.size, q.size);
		if (q.size)
			EXPECT_EQ(std::string(reinterpret_cast<const char *>(
						      p.data),
					  p.size),
				std::string(
					reinterpret_cast<const char *>(q.data),
					q.size));
	}
}

/* Independent XML comparison ignores attribute order and quote spelling. */
static void XMLCALL start(
	void *state, const XML_Char *name, const XML_Char **attributes)
{
	auto &result = *static_cast<std::string *>(state);
	std::map<std::string, std::string> sorted;

	for (size_t i = 0; attributes[i]; i += 2)
		sorted[attributes[i]] = attributes[i + 1];
	result += "<" + std::string(name);
	for (const auto &item : sorted)
		result += " " + item.first + "=" + item.second;
	result += ">";
}

static void XMLCALL end(void *state, const XML_Char *name)
{
	*static_cast<std::string *>(state) += "</" + std::string(name) + ">";
}

static void XMLCALL characters(void *state, const XML_Char *text, int size)
{
	static_cast<std::string *>(state)->append(text, size);
}

std::string canonical(const std::string &value)
{
	XML_Parser parser = XML_ParserCreate(nullptr);
	std::string result;
	std::string wrapped = "<root>" + value + "</root>";

	XML_SetUserData(parser, &result);
	XML_SetElementHandler(parser, start, end);
	XML_SetCharacterDataHandler(parser, characters);
	EXPECT_EQ(XML_Parse(parser, wrapped.data(), wrapped.size(), true),
		XML_STATUS_OK)
		<< value;
	XML_ParserFree(parser);
	return result;
}

std::string encoded_event(cot_format kind, const cot_event &value)
{
	owned_buffer buffer;

	EXPECT_EQ(cot_encode(kind, &value, &buffer.value), COT_STATUS_OK);
	return bytes(buffer.value);
}

void set_typed(EventContext &c, const std::string &name)
{
	c.caller.detail = &c.detail;
	if (name == "contact")
		c.detail.contact = &c.contact;
	else if (name == "group")
		c.detail.group = &c.group;
	else if (name == "precision_location")
		c.detail.precision_location = &c.precision;
	else if (name == "status")
		c.detail.status = &c.status_detail;
	else if (name == "takv")
		c.detail.takv = &c.takv;
	else if (name == "track")
		c.detail.track = &c.track;
	else
		ADD_FAILURE() << "Unknown typed detail " << name;
	c.message = name;
}

const void *typed(const cot_detail *d, const std::string &name)
{
	if (!d)
		return nullptr;
	if (name == "contact")
		return d->contact;
	if (name == "group")
		return d->group;
	if (name == "precision_location")
		return d->precision_location;
	if (name == "status")
		return d->status;
	if (name == "takv")
		return d->takv;
	if (name == "track")
		return d->track;
	ADD_FAILURE() << "Unknown typed detail " << name;
	return nullptr;
}

void replace(std::string &value, const std::string &from, const std::string &to)
{
	auto at = value.find(from);

	ASSERT_NE(at, std::string::npos) << from;
	value.replace(at, from.size(), to);
}

std::string fixture(const std::string &name)
{
	std::string input = xml();

	if (name == "empty bytes")
		return "";
	if (name == "malformed XML" ||
		name == "XML with a mismatched closing tag")
		replace(input, "</event>", "</wrong>");
	else if (name == "XML with two complete event documents")
		input += input;
	else if (name == "XML with a required point attribute missing")
		replace(input, "lat='38' ", "");
	else if (name == "XML with a required event attribute missing")
		replace(input, "uid='alpha-1' ", "");
	else if (name == "XML with duplicate point elements")
		replace(input, "<detail>",
			"<point lat='0' lon='0' hae='0' ce='0' "
			"le='0'/><detail>");
	else if (name == "XML with duplicate detail elements")
		replace(input, "</event>", "<detail/></event>");
	else if (name == "XML with timestamp 2024-02-30T00:00:00Z")
		replace(input, "2024-01-01T00:00:00.123Z",
			"2024-02-30T00:00:00Z");
	else if (name == "XML with latitude text \"north\"")
		replace(input, "lat='38'", "lat='north'");
	else if (name == "XML with invalid UTF-8")
		replace(input, "alpha-1", std::string(1, '\xff'));
	else if (name == "truncated Protobuf" ||
		 name == "Protobuf with a truncated length-delimited field")
		return std::string("\x12\x10\x01", 3);
	else if (name == "Protobuf with an unterminated varint")
		return std::string("\x80", 1);
	else if (name ==
		 "Protobuf with invalid UTF-8 in a known string field") {
		auto wire =
			protobuf_event(wire_string(5, std::string(1, '\xff')));

		return {wire.begin(), wire.end()};
	} else if (name == "neither control nor event")
		return "";
	else if (name == "a control only" ||
		 name == "a control-only TakMessage")
		return std::string("\x0a\x00", 2);
	else if (name == "an event and a control" ||
		 name == "an event and an empty control" ||
		 name == "an event-plus-control TakMessage")
		return std::string("\x0a\x00", 2) +
		       encoded_event(COT_FORMAT_PROTOBUF, event());
	else if (name == "version \"3.0\"")
		replace(input, "version='2.0'", "version='3.0'");
	else if (name == "an unrecognized event attribute")
		replace(input, "<event ", "<event custom='x' ");
	else if (name == "an unrecognized point attribute")
		replace(input, "<point ", "<point custom='x' ");
	else if (name == "a child outside point and detail")
		replace(input, "</event>", "<extra/></event>");
	else if (name == "a DTD declaring an external entity")
		input = "<!DOCTYPE event [<!ENTITY x SYSTEM "
			"'file:///missing'>]>" +
			input;
	else if (name == "a timestamp before epoch zero")
		replace(input, "2024-01-01T00:00:00.123Z",
			"1969-12-31T23:59:59Z");
	else if (name == "a timestamp with nonzero submillisecond precision")
		replace(input, "2024-01-01T00:00:00.123Z",
			"2024-01-01T00:00:00.1234Z");
	else if (name == "latitude 91")
		replace(input, "lat='38'", "lat='91'");
	else if (name == "an event with an XML-inexpressible timestamp") {
		cot_event value = event();

		value.send_time_ms = UINT64_MAX;
		return encoded_event(COT_FORMAT_PROTOBUF, value);
	} else if (name == "an event with an opaque extension") {
		cot_event value = event();
		cot_extension extension = {42, {nullptr, 0}};
		cot_detail detail{};

		detail.extensions = &extension;
		detail.extension_count = 1;
		value.detail = &detail;
		return encoded_event(COT_FORMAT_PROTOBUF, value);
	} else {
		ADD_FAILURE() << "Unknown fixture: " << name;
	}
	return input;
}
