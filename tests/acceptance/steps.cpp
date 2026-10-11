#include "context.h"

namespace
{

GIVEN("^a caller-owned event with type \"([^\"]*)\", uid \"([^\"]*)\", and how "
	"\"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, type);
	REGEX_PARAM(std::string, uid);
	REGEX_PARAM(std::string, how);
	ScenarioScope<EventContext> c;

	c->caller.type = c->keep(type);
	c->caller.uid = c->keep(uid);
	c->caller.how = c->keep(how);
}

GIVEN("^(?:its |the )send, start, and stale times are ([0-9]+), ([0-9]+), and "
	"([0-9]+) milliseconds$")
{
	REGEX_PARAM(uint64_t, send);
	REGEX_PARAM(uint64_t, start);
	REGEX_PARAM(uint64_t, stale);
	ScenarioScope<EventContext> c;

	if (c->view()) {
		EXPECT_EQ(c->view()->send_time_ms, send);
		EXPECT_EQ(c->view()->start_time_ms, start);
		EXPECT_EQ(c->view()->stale_time_ms, stale);
		return;
	}
	c->caller.send_time_ms = send;
	c->caller.start_time_ms = start;
	c->caller.stale_time_ms = stale;
}

GIVEN("^(?:its |the )point is latitude ([-0-9.]+), longitude ([-0-9.]+), HAE "
	"([-0-9.]+), CE ([-0-9.]+), and LE ([-0-9.]+)$")
{
	REGEX_PARAM(double, lat);
	REGEX_PARAM(double, lon);
	REGEX_PARAM(double, hae);
	REGEX_PARAM(double, ce);
	REGEX_PARAM(double, le);
	ScenarioScope<EventContext> c;

	c->caller.point = {lat, lon, hae, ce, le};
}

GIVEN("^it has no detail$")
{
	ScenarioScope<EventContext> c;

	c->caller.detail = nullptr;
}

WHEN("^I encode the event as (XML|Protobuf) into an empty buffer$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<EventContext> c;

	ASSERT_EQ(c->output.value.data, nullptr);
	c->encode(format(kind), &c->caller);
}

WHEN("^I decode the entire result as (XML|Protobuf) into a null owner$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<EventContext> c;

	ASSERT_EQ(c->owner.value, nullptr);
	c->decode(format(kind), bytes(c->output.value));
}

THEN("^(?:both|all) calls return COT_STATUS_OK$")
{
	ScenarioScope<EventContext> c;

	ASSERT_FALSE(c->calls.empty());
	for (cot_status call : c->calls)
		EXPECT_EQ(call, COT_STATUS_OK);
}

THEN("^(?:the decoded view has the same represented core fields|the "
	"represented core fields equal the original event)$")
{
	ScenarioScope<EventContext> c;

	same_core(c->view(), &c->caller);
}

THEN("^its detail pointer is null$")
{
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	EXPECT_EQ(c->view()->detail, nullptr);
}

GIVEN("^a (XML|Protobuf) message representing the event$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<EventContext> c;

	c->input_format = format(kind);
	c->input = encoded_event(c->input_format, c->caller);
}

WHEN("^I decode it and encode its view as (XML|Protobuf)$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<EventContext> c;

	c->decode(c->input_format, c->input);
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
	c->encode(format(kind), c->view());
	ASSERT_EQ(c->encoded, COT_STATUS_OK);
}

WHEN("^I decode that result as (XML|Protobuf)$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<EventContext> c;

	c->decode(format(kind), bytes(c->output.value));
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
}

static cot_text *core_text(cot_event &event, const std::string &name)
{
	if (name == "access")
		return &event.access;
	if (name == "qos")
		return &event.qos;
	if (name == "opex")
		return &event.opex;
	if (name == "caveat")
		return &event.caveat;
	if (name == "releasable_to")
		return &event.releasable_to;
	return &event.uid;
}

GIVEN("^the event has (access|qos|opex|caveat|releasable_to) set to counted "
	"UTF-8 text \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, name);
	REGEX_PARAM(std::string, value);
	ScenarioScope<EventContext> c;

	*core_text(c->caller, name) = c->keep(value);
}

WHEN("^I encode as XML, decode, encode as Protobuf, and decode again$")
{
	ScenarioScope<EventContext> c;

	c->roundtrip({COT_FORMAT_XML, COT_FORMAT_PROTOBUF});
}

THEN("^the decoded (access|qos|opex|caveat|releasable_to) is exactly the "
	"counted UTF-8 text \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, name);
	REGEX_PARAM(std::string, value);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	cot_event view = *c->view();

	EXPECT_EQ(string(*core_text(view, name)), value);
}

GIVEN("^XML for the event with (no access attribute|an empty access "
	"attribute|access set to \"Undefined\")$")
{
	REGEX_PARAM(std::string, access);
	ScenarioScope<EventContext> c;

	c->input = xml();
	if (access != "no access attribute")
		replace(c->input, "<event ",
			access == "an empty access attribute"
				? "<event access='' "
				: "<event access='Undefined' ");
}

WHEN("^I decode (?:it|the XML), encode as Protobuf, decode, and encode as XML$")
{
	ScenarioScope<EventContext> c;

	c->decode(c->input_format, c->input);
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
	c->roundtrip({COT_FORMAT_PROTOBUF});
	c->encode(COT_FORMAT_XML, c->view());
}

THEN("^the decoded access is empty$")
{
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	EXPECT_EQ(c->view()->access.size, 0U);
}

THEN("^the output XML has no access attribute$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(bytes(c->output.value).find("access="), std::string::npos);
}

GIVEN("^the uid points to the first 7 bytes of \"alpha-1ignored\"$")
{
	ScenarioScope<EventContext> c;

	c->caller.uid = c->keep("alpha-1ignored");
	c->caller.uid.size = 7;
}

WHEN("^I encode as (XML|Protobuf) and decode the result$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<EventContext> c;

	c->roundtrip({format(kind)});
}

THEN("^the decoded uid is exactly 7 bytes containing \"alpha-1\"$")
{
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	EXPECT_EQ(c->view()->uid.size, 7U);
	EXPECT_EQ(string(c->view()->uid), "alpha-1");
}

GIVEN("^all three event timestamps are ([0-9]+)$")
{
	REGEX_PARAM(uint64_t, value);
	ScenarioScope<EventContext> c;

	c->caller.send_time_ms = value;
	c->caller.start_time_ms = value;
	c->caller.stale_time_ms = value;
}

THEN("^all three timestamps are ([0-9]+)$")
{
	REGEX_PARAM(uint64_t, value);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	EXPECT_EQ(c->view()->send_time_ms, value);
	EXPECT_EQ(c->view()->start_time_ms, value);
	EXPECT_EQ(c->view()->stale_time_ms, value);
}

THEN("^the XML timestamps are \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, value);
	ScenarioScope<EventContext> c;
	std::string output = bytes(c->output.value);

	for (const char *name : {"time", "start", "stale"})
		EXPECT_NE(output.find(std::string(name) + "=\"" + value + "\""),
			std::string::npos);
}

GIVEN("^XML for the event with all timestamps \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, value);
	ScenarioScope<EventContext> c;

	c->input = xml();
	for (const char *old : {"2024-01-01T00:00:00.123Z",
		     "2024-01-01T00:00:00.123Z", "2024-01-01T00:01:00.123Z"})
		replace(c->input, old, value);
}

WHEN("^I decode the XML$")
{
	ScenarioScope<EventContext> c;

	c->decode(COT_FORMAT_XML, c->input);
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
}

WHEN("^I encode (?:the decoded view|the view) as XML(?: into an empty "
	"buffer)?$")
{
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	c->encode(COT_FORMAT_XML, c->view());
}

THEN("^encoding returns (COT_STATUS_[A-Z_]+)$")
{
	REGEX_PARAM(std::string, name);
	ScenarioScope<EventContext> c;
	cot_status expected = name == "COT_STATUS_OK" ? COT_STATUS_OK
			      : name == "COT_STATUS_INVALID_ARGUMENT"
				      ? COT_STATUS_INVALID_ARGUMENT
				      : COT_STATUS_UNSUPPORTED_CONTENT;

	EXPECT_EQ(c->encoded, expected);
}

THEN("^the output buffer remains empty$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(c->output.value.data, nullptr);
	EXPECT_EQ(c->output.value.size, 0U);
}

THEN("^the decoded point is latitude ([-0-9.]+), longitude ([-0-9.]+), HAE "
	"([-0-9.]+), CE ([-0-9.]+), and LE ([-0-9.]+)$")
{
	REGEX_PARAM(double, lat);
	REGEX_PARAM(double, lon);
	REGEX_PARAM(double, hae);
	REGEX_PARAM(double, ce);
	REGEX_PARAM(double, le);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	EXPECT_EQ(c->view()->point.latitude, lat);
	EXPECT_EQ(c->view()->point.longitude, lon);
	EXPECT_EQ(c->view()->point.hae, hae);
	EXPECT_EQ(c->view()->point.ce, ce);
	EXPECT_EQ(c->view()->point.le, le);
}

GIVEN("^the event has (no detail|a present empty detail)$")
{
	REGEX_PARAM(std::string, presence);
	ScenarioScope<EventContext> c;

	c->caller.detail = presence == "no detail" ? nullptr : &c->detail;
}

THEN("^the decoded detail is (absent|present with no children)$")
{
	REGEX_PARAM(std::string, presence);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	if (presence == "absent") {
		EXPECT_EQ(c->view()->detail, nullptr);
		return;
	}
	ASSERT_NE(c->view()->detail, nullptr);
	EXPECT_EQ(c->view()->detail->xml.size, 0U);
	EXPECT_EQ(c->view()->detail->extension_count, 0U);
	for (const char *name : {"contact", "group", "precision_location",
		     "status", "takv", "track"})
		EXPECT_EQ(typed(c->view()->detail, name), nullptr);
}

GIVEN("^XML for the event with a self-closing detail element$")
{
	ScenarioScope<EventContext> c;

	c->input = xml();
	replace(c->input, "<detail></detail>", "<detail/>");
}

WHEN("^I decode (?:it|the XML), encode as Protobuf, and decode again$")
{
	ScenarioScope<EventContext> c;

	c->decode(c->input_format, c->input);
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
	c->roundtrip({COT_FORMAT_PROTOBUF});
}

GIVEN("^the event has a detail with a present all-zero "
	"(contact|group|precision_location|status|takv|track) struct$")
{
	REGEX_PARAM(std::string, name);
	ScenarioScope<EventContext> c;

	set_typed(*c, name);
}

THEN("^the decoded (contact|group|precision_location|status|takv|track) "
	"pointer is nonnull$")
{
	REGEX_PARAM(std::string, name);
	ScenarioScope<EventContext> c;

	ASSERT_NE(c->view(), nullptr);
	EXPECT_NE(typed(c->view()->detail, name), nullptr);
}

THEN("^every scalar in that message has its default value$")
{
	ScenarioScope<EventContext> c;

	same_event(c->view(), &c->caller);
}

GIVEN("^XML for the event using single quotes, entities, and reordered "
	"attributes$")
{
	ScenarioScope<EventContext> c;

	c->input = xml();
	replace(c->input, "type='a-f-G-U-C' uid='alpha-1'",
		"uid='alpha&#45;1' type='a-f-G-U-C'");
}

THEN("^the output XML has the same represented values$")
{
	ScenarioScope<EventContext> c;
	owned_event parsed;

	ASSERT_EQ(decode(COT_FORMAT_XML, bytes(c->output.value), parsed),
		COT_STATUS_OK);
	same_core(parsed.get(), &c->caller);
}

THEN("^no assertion depends on XML formatting or Protobuf field order$")
{
	ScenarioScope<EventContext> c;
	owned_event original;
	owned_event result;

	ASSERT_EQ(decode(COT_FORMAT_XML, c->input, original), COT_STATUS_OK);
	ASSERT_EQ(decode(COT_FORMAT_XML, bytes(c->output.value), result),
		COT_STATUS_OK);
	same_event(result.get(), original.get());
}

} // namespace
