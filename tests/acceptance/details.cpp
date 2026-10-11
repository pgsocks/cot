#include "context.h"
#include <limits>

namespace
{

GIVEN("^a valid XML CoT event with an otherwise empty detail$")
{
	ScenarioScope<EventContext> c;

	c->input = xml();
	c->caller.detail = &c->detail;
}

GIVEN("^the detail contains exactly (.*)$")
{
	REGEX_PARAM(std::string, element);
	ScenarioScope<EventContext> c;

	c->original_xml = element;
	c->input = xml(element);
}

THEN("^the (contact|group|precision_location|status|takv|track) message is "
	"present with (.*)$")
{
	REGEX_PARAM(std::string, name);
	REGEX_PARAM(std::string, values);
	ScenarioScope<EventContext> c;

	set_typed(*c, name);
	if (name == "contact") {
		c->contact.callsign = c->keep("Alpha");
		c->contact.endpoint = c->keep("a:1:tcp");
		c->contact.altendpoints = c->keep("b:2:udp");
	} else if (name == "group") {
		c->group.name = c->keep("Blue");
		c->group.role = c->keep("Team Member");
	} else if (name == "precision_location") {
		c->precision.geopointsrc = c->keep("GPS");
		c->precision.altsrc = c->keep("DTED");
	} else if (name == "takv") {
		c->takv.device = c->keep("phone");
		c->takv.platform = c->keep("ATAK");
		c->takv.os = c->keep("Android");
		c->takv.version = c->keep("5.5.1.8");
	}
	ASSERT_FALSE(values.empty());
	same_event(c->view(), &c->caller);
}

THEN("^residual XML contains no "
	"(contact|__group|precisionlocation|status|takv|track) element$")
{
	REGEX_PARAM(std::string, tag);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	ASSERT_NE(c->view()->detail, nullptr);
	EXPECT_EQ(string(c->view()->detail->xml).find("<" + tag),
		std::string::npos);
}

static std::string detail_children(const std::string &document)
{
	auto begin = document.find("<detail>");
	auto end = document.find("</detail>");

	if (begin == std::string::npos || end == std::string::npos) {
		ADD_FAILURE() << document;
		return "";
	}
	return document.substr(begin + 8, end - begin - 8);
}

THEN("^the detail contains an equivalent "
	"(contact|__group|precisionlocation|status|takv|track) element$")
{
	REGEX_PARAM(std::string, tag);
	ScenarioScope<EventContext> c;

	ASSERT_EQ(c->encoded, COT_STATUS_OK);
	EXPECT_EQ(canonical(detail_children(bytes(c->output.value))),
		canonical(c->original_xml));
	EXPECT_NE(bytes(c->output.value).find("<" + tag), std::string::npos);
}

THEN("^contact is present with callsign \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, value);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	ASSERT_NE(c->view()->detail, nullptr);
	ASSERT_NE(c->view()->detail->contact, nullptr);
	EXPECT_EQ(string(c->view()->detail->contact->callsign), value);
}

THEN("^its endpoint and altendpoints are empty$")
{
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view()->detail->contact, nullptr);
	EXPECT_EQ(c->view()->detail->contact->endpoint.size, 0U);
	EXPECT_EQ(c->view()->detail->contact->altendpoints.size, 0U);
}

THEN("^status is present with battery ([0-9]+)$")
{
	REGEX_PARAM(uint64_t, value);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	ASSERT_NE(c->view()->detail, nullptr);
	ASSERT_NE(c->view()->detail->status, nullptr);
	EXPECT_EQ(c->view()->detail->status->battery, value);
}

THEN("^the (contact|group|precision_location|status|takv|track) pointer is "
	"null$")
{
	REGEX_PARAM(std::string, name);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	EXPECT_EQ(typed(c->view()->detail, name), nullptr);
}

THEN("^residual XML contains the entire original element semantically$")
{
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view()->detail, nullptr);
	EXPECT_EQ(canonical(string(c->view()->detail->xml)),
		canonical(c->original_xml));
}

GIVEN("^the detail contains (<contact .*/>) followed by (<contact .*/>)$")
{
	REGEX_PARAM(std::string, first);
	REGEX_PARAM(std::string, second);
	ScenarioScope<EventContext> c;

	c->original_xml = first + second;
	c->input = xml(c->original_xml);
}

THEN("^both contact elements remain in residual XML in their original order$")
{
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view()->detail, nullptr);
	EXPECT_EQ(canonical(string(c->view()->detail->xml)),
		canonical(c->original_xml));
}

GIVEN("^the detail children are:$")
{
	REGEX_PARAM(std::string, children);
	ScenarioScope<EventContext> c;

	c->original_xml = children;
	c->input = xml(children);
}

THEN("^the detail children retain equivalent text, attributes, and nesting$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(canonical(detail_children(bytes(c->output.value))),
		canonical(c->original_xml));
}

THEN("^no standard typed message is populated$")
{
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view()->detail, nullptr);
	for (const char *name : {"contact", "group", "precision_location",
		     "status", "takv", "track"})
		EXPECT_EQ(typed(c->view()->detail, name), nullptr);
}

GIVEN("^the detail contains (<contact .*/>) and (<custom .*/>)$")
{
	REGEX_PARAM(std::string, contact);
	REGEX_PARAM(std::string, residual);
	ScenarioScope<EventContext> c;

	c->original_xml = residual;
	c->input = xml(contact + residual);
}

THEN("^residual XML contains only the custom element semantically$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(canonical(string(c->view()->detail->xml)),
		canonical(c->original_xml));
}

GIVEN("^a Protobuf event with residual (?:XML containing )?(<.*>)$")
{
	REGEX_PARAM(std::string, residual);
	ScenarioScope<EventContext> c;

	c->original_xml = residual;
	c->detail.xml = c->keep(residual);
	c->caller.detail = &c->detail;
	c->input_format = COT_FORMAT_PROTOBUF;
}

GIVEN("^a present typed (contact|group|precision_location|status|takv|track) "
	"with different values$")
{
	REGEX_PARAM(std::string, name);
	ScenarioScope<EventContext> c;

	set_typed(*c, name);
	c->contact.callsign = c->keep("Typed");
	c->group = {c->keep("Typed"), c->keep("Member")};
	c->precision = {c->keep("T"), c->keep("T")};
	c->status_detail.battery = 99;
	c->takv = {c->keep("T"), c->keep("T"), c->keep("T"), c->keep("T")};
	c->track = {9, 10};
	c->input = encoded_event(COT_FORMAT_PROTOBUF, c->caller);
}

WHEN("^I decode the Protobuf and encode its view as XML$")
{
	ScenarioScope<EventContext> c;

	c->decode(COT_FORMAT_PROTOBUF, c->input);
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
	c->encode(COT_FORMAT_XML, c->view());
	ASSERT_EQ(c->encoded, COT_STATUS_OK);
}

THEN("^the output retains the residual "
	"(contact|__group|precisionlocation|status|takv|track) element"
	" unchanged "
	"semantically$")
{
	REGEX_PARAM(std::string, tag);
	ScenarioScope<EventContext> c;

	EXPECT_NE(bytes(c->output.value).find("<" + tag), std::string::npos);
	EXPECT_EQ(canonical(detail_children(bytes(c->output.value))),
		canonical(c->original_xml));
}

THEN("^no second (contact|__group|precisionlocation|status|takv|track) element "
	"is synthesized from the typed message$")
{
	REGEX_PARAM(std::string, tag);
	ScenarioScope<EventContext> c;
	std::string output = bytes(c->output.value);
	auto first = output.find("<" + tag);

	ASSERT_NE(first, std::string::npos);
	EXPECT_EQ(output.find("<" + tag, first + 1), std::string::npos);
}

GIVEN("^a present typed track with NaN speed and infinite course$")
{
	ScenarioScope<EventContext> c;

	set_typed(*c, "track");
	c->track = {std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity()};
	c->input = encoded_event(COT_FORMAT_PROTOBUF, c->caller);
}

WHEN("^I decode, encode as Protobuf, and decode again$")
{
	ScenarioScope<EventContext> c;

	c->decode(COT_FORMAT_PROTOBUF, c->input);
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
	c->roundtrip({COT_FORMAT_PROTOBUF});
}

THEN("^both the residual track and the typed nonfinite values remain$")
{
	ScenarioScope<EventContext> c;

	same_event(c->view(), &c->caller);
}

THEN("^the XML contains only the residual track$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(canonical(detail_children(bytes(c->output.value))),
		canonical(c->original_xml));
}

GIVEN("^a caller-owned valid event with these extension entries in order:$")
{
	TABLE_PARAM(entries);
	ScenarioScope<EventContext> c;

	for (const auto &row : entries.hashes()) {
		std::string hex = row.at("hexadecimal bytes");
		std::string data;

		for (size_t i = 0; i < hex.size(); i += 2)
			data += static_cast<char>(
				std::stoul(hex.substr(i, 2), nullptr, 16));
		cot_text span = c->keep(data);

		c->extensions.push_back(
			{static_cast<uint32_t>(std::stoul(row.at("id"))),
				{reinterpret_cast<const uint8_t *>(span.data),
					span.size}});
	}
	c->detail.extensions = c->extensions.data();
	c->detail.extension_count = c->extensions.size();
	c->caller.detail = &c->detail;
}

WHEN("^I encode as Protobuf, decode, encode as Protobuf, and decode again$")
{
	ScenarioScope<EventContext> c;

	c->roundtrip({COT_FORMAT_PROTOBUF, COT_FORMAT_PROTOBUF});
}

THEN("^the extension array has the same IDs and bytes in the same order$")
{
	ScenarioScope<EventContext> c;

	same_event(c->view(), &c->caller);
}

GIVEN("^a Protobuf event with one extension entry of id 42 and (empty "
	"bytes|hexadecimal bytes 00ff)$")
{
	REGEX_PARAM(std::string, data);
	ScenarioScope<EventContext> c;
	cot_text span = c->keep(
		data == "empty bytes" ? "" : std::string("\x00\xff", 2));

	c->extensions.push_back({42,
		{reinterpret_cast<const uint8_t *>(span.data), span.size}});
	c->detail.extensions = c->extensions.data();
	c->detail.extension_count = 1;
	c->caller.detail = &c->detail;
	c->input = encoded_event(COT_FORMAT_PROTOBUF, c->caller);
}

WHEN("^I decode the Protobuf$")
{
	ScenarioScope<EventContext> c;

	c->decode(COT_FORMAT_PROTOBUF, c->input);
}

THEN("^track is represented with speed -1 and course 360$")
{
	ScenarioScope<EventContext> c;
	owned_event result;

	ASSERT_EQ(decode(COT_FORMAT_XML, bytes(c->output.value), result),
		COT_STATUS_OK);
	ASSERT_NE(result.get()->detail, nullptr);
	ASSERT_NE(result.get()->detail->track, nullptr);
	EXPECT_EQ(result.get()->detail->track->speed, -1);
	EXPECT_EQ(result.get()->detail->track->course, 360);
}

} // namespace
