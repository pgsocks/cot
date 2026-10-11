#include "support.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace cot_test;

std::string string(cot_text value)
{
	return value.size ? std::string(value.data, value.size) : std::string();
}

void expect_number(double expected, double actual)
{
	if (std::isnan(expected))
		EXPECT_TRUE(std::isnan(actual));
	else
		EXPECT_EQ(expected, actual);
}

void expect_point(const cot_point &expected, const cot_point &actual)
{
	expect_number(expected.latitude, actual.latitude);
	expect_number(expected.longitude, actual.longitude);
	expect_number(expected.hae, actual.hae);
	expect_number(expected.ce, actual.ce);
	expect_number(expected.le, actual.le);
}

void expect_core(const cot_event &expected, const cot_event &actual)
{
	EXPECT_EQ(string(expected.type), string(actual.type));
	EXPECT_EQ(string(expected.access), string(actual.access));
	EXPECT_EQ(string(expected.qos), string(actual.qos));
	EXPECT_EQ(string(expected.opex), string(actual.opex));
	EXPECT_EQ(string(expected.uid), string(actual.uid));
	EXPECT_EQ(expected.send_time_ms, actual.send_time_ms);
	EXPECT_EQ(expected.start_time_ms, actual.start_time_ms);
	EXPECT_EQ(expected.stale_time_ms, actual.stale_time_ms);
	EXPECT_EQ(string(expected.how), string(actual.how));
	expect_point(expected.point, actual.point);
	EXPECT_EQ(string(expected.caveat), string(actual.caveat));
	EXPECT_EQ(string(expected.releasable_to), string(actual.releasable_to));
}

std::string replace(std::string input, const std::string &from,
	const std::string &to)
{
	size_t at = input.find(from);

	EXPECT_NE(std::string::npos, at);
	if (at != std::string::npos)
		input.replace(at, from.size(), to);
	return input;
}

std::string document(const std::string &timestamp =
	"2024-01-01T00:00:00.123Z")
{
	return "<event version=\"2.0\" type=\"a-f-G-U-C\" uid=\"alpha-1\" "
		"how=\"m-g\" time=\"" + timestamp + "\" start=\"" + timestamp +
		"\" stale=\"" + timestamp + "\"><point lat=\"38\" lon=\"-77\" "
		"hae=\"10\" ce=\"2\" le=\"3\"/></event>";
}

std::vector<uint8_t> core_fields(const cot_event &value)
{
	std::vector<uint8_t> fields;

	append(fields, wire_string(1, string(value.type)));
	append(fields, wire_string(2, string(value.access)));
	append(fields, wire_string(3, string(value.qos)));
	append(fields, wire_string(4, string(value.opex)));
	append(fields, wire_string(5, string(value.uid)));
	append(fields, wire_uint(6, value.send_time_ms));
	append(fields, wire_uint(7, value.start_time_ms));
	append(fields, wire_uint(8, value.stale_time_ms));
	append(fields, wire_string(9, string(value.how)));
	append(fields, wire_double(10, value.point.latitude));
	append(fields, wire_double(11, value.point.longitude));
	append(fields, wire_double(12, value.point.hae));
	append(fields, wire_double(13, value.point.ce));
	append(fields, wire_double(14, value.point.le));
	append(fields, wire_string(16, string(value.caveat)));
	append(fields, wire_string(17, string(value.releasable_to)));
	return fields;
}

class event_conversion : public testing::TestWithParam<
	std::pair<cot_format, cot_format>> {};

TEST_P(event_conversion, PreservesEveryCoreFieldAndOptionalString)
{
	cot_event value = event();
	const std::string access = u8"access-é<&\"'";
	const std::string qos = u8"qos-é<&\"'";
	const std::string opex = u8"opex-é<&\"'";
	const std::string caveat = u8"caveat-é<&\"'";
	const std::string releasable = u8"release-é<&\"'";

	value.access = text(access);
	value.qos = text(qos);
	value.opex = text(opex);
	value.caveat = text(caveat);
	value.releasable_to = text(releasable);
	owned_buffer first;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(GetParam().first, &value, &first.value));
	owned_event source;

	ASSERT_EQ(COT_STATUS_OK,
		decode(GetParam().first, bytes(first.value), source));
	ASSERT_NE(nullptr, source.get());
	expect_core(value, *source.get());
	EXPECT_EQ(nullptr, source.get()->detail);
	owned_buffer converted;

	ASSERT_EQ(COT_STATUS_OK, cot_encode(GetParam().second, source.get(),
		&converted.value));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(GetParam().second, bytes(converted.value), result));
	ASSERT_NE(nullptr, result.get());
	expect_core(value, *result.get());
	EXPECT_EQ(nullptr, result.get()->detail);
}

INSTANTIATE_TEST_SUITE_P(BothFormats, event_conversion, testing::Values(
	std::make_pair(COT_FORMAT_XML, COT_FORMAT_XML),
	std::make_pair(COT_FORMAT_XML, COT_FORMAT_PROTOBUF),
	std::make_pair(COT_FORMAT_PROTOBUF, COT_FORMAT_XML),
	std::make_pair(COT_FORMAT_PROTOBUF, COT_FORMAT_PROTOBUF)));

TEST(EventWire, ReadsEveryOfficialFieldFromIndependentFixture)
{
	cot_event expected = event();
	const std::string access = "Unclassified";
	const std::string qos = "7-r-c";
	const std::string opex = "e-exercise";
	const std::string caveat = "caveat";
	const std::string releasable = "USA, CAN";

	expected.access = text(access);
	expected.qos = text(qos);
	expected.opex = text(opex);
	expected.caveat = text(caveat);
	expected.releasable_to = text(releasable);
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		protobuf_event(core_fields(expected)), result));
	ASSERT_NE(nullptr, result.get());
	expect_core(expected, *result.get());
}

TEST(EventWire, AcceptsAbsentProto3ScalarsAsDefaults)
{
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, protobuf_event({}), result));
	ASSERT_NE(nullptr, result.get());
	cot_event expected{};

	expect_core(expected, *result.get());
	EXPECT_EQ(nullptr, result.get()->detail);
	owned_buffer encoded;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, result.get(), &encoded.value));
	EXPECT_EQ(nullptr, encoded.value.data);
	EXPECT_EQ(0u, encoded.value.size);
}

TEST(EventWire, LastDuplicateScalarWinsIncludingAnExplicitDefault)
{
	std::vector<uint8_t> fields = core_fields(event());

	append(fields, wire_string(5, "last"));
	append(fields, wire_string(5, ""));
	append(fields, wire_uint(6, 987));
	append(fields, wire_uint(6, 0));
	append(fields, wire_double(10, 80));
	append(fields, wire_double(10, 0));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		protobuf_event(fields), result));
	ASSERT_NE(nullptr, result.get());
	EXPECT_EQ("", string(result.get()->uid));
	EXPECT_EQ(0u, result.get()->send_time_ms);
	EXPECT_EQ(0.0, result.get()->point.latitude);
	EXPECT_EQ(-77.0, result.get()->point.longitude);
}

TEST(EventWire, MergesRepeatedSingularEventMessages)
{
	std::vector<uint8_t> input = protobuf_event(wire_string(5, "first"));
	std::vector<uint8_t> second = wire_string(1, "new-type");

	append(second, wire_uint(6, 321));
	append(input, protobuf_event(second));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF, input, result));
	ASSERT_NE(nullptr, result.get());
	EXPECT_EQ("first", string(result.get()->uid));
	EXPECT_EQ("new-type", string(result.get()->type));
	EXPECT_EQ(321u, result.get()->send_time_ms);
}

TEST(EventWire, MergesRepeatedSingularDetailsAndTheirChildren)
{
	std::vector<uint8_t> first = wire_message(2,
		wire_string(1, "endpoint"));
	std::vector<uint8_t> second = wire_message(2,
		wire_string(2, "callsign"));
	std::vector<uint8_t> fields = wire_message(15, first);

	append(fields, wire_message(15, second));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		protobuf_event(fields), result));
	ASSERT_NE(nullptr, result.get());
	ASSERT_NE(nullptr, result.get()->detail);
	ASSERT_NE(nullptr, result.get()->detail->contact);
	EXPECT_EQ("endpoint", string(result.get()->detail->contact->endpoint));
	EXPECT_EQ("callsign", string(result.get()->detail->contact->callsign));
}

TEST(EventWire, PreservesEmptyNestedMessagePresence)
{
	std::vector<uint8_t> detail;

	for (unsigned field = 2; field <= 7; ++field)
		append(detail, wire_message(field, {}));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		protobuf_event(wire_message(15, detail)), result));
	ASSERT_NE(nullptr, result.get());
	ASSERT_NE(nullptr, result.get()->detail);
	const cot_detail *view = result.get()->detail;

	ASSERT_NE(nullptr, view->contact);
	ASSERT_NE(nullptr, view->group);
	ASSERT_NE(nullptr, view->precision_location);
	ASSERT_NE(nullptr, view->status);
	ASSERT_NE(nullptr, view->takv);
	ASSERT_NE(nullptr, view->track);
	EXPECT_EQ("", string(view->contact->endpoint));
	EXPECT_EQ("", string(view->contact->callsign));
	EXPECT_EQ("", string(view->contact->altendpoints));
	EXPECT_EQ("", string(view->group->name));
	EXPECT_EQ("", string(view->group->role));
	EXPECT_EQ("", string(view->precision_location->geopointsrc));
	EXPECT_EQ("", string(view->precision_location->altsrc));
	EXPECT_EQ(0u, view->status->battery);
	EXPECT_EQ("", string(view->takv->device));
	EXPECT_EQ("", string(view->takv->platform));
	EXPECT_EQ("", string(view->takv->os));
	EXPECT_EQ("", string(view->takv->version));
	EXPECT_EQ(0.0, view->track->speed);
	EXPECT_EQ(0.0, view->track->course);
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, result.get(), &encoded.value));
	owned_event again;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(encoded.value), again));
	ASSERT_NE(nullptr, again.get());
	ASSERT_NE(nullptr, again.get()->detail);
	EXPECT_NE(nullptr, again.get()->detail->contact);
	EXPECT_NE(nullptr, again.get()->detail->group);
	EXPECT_NE(nullptr, again.get()->detail->precision_location);
	EXPECT_NE(nullptr, again.get()->detail->status);
	EXPECT_NE(nullptr, again.get()->detail->takv);
	EXPECT_NE(nullptr, again.get()->detail->track);
}

class event_presence : public testing::TestWithParam<cot_format> {};

TEST_P(event_presence, DistinguishesAbsentAndPresentEmptyDetail)
{
	for (bool present : {false, true}) {
		SCOPED_TRACE(present);
		cot_event value = event();
		cot_detail detail{};

		value.detail = present ? &detail : nullptr;
		owned_buffer encoded;

		ASSERT_EQ(COT_STATUS_OK,
			cot_encode(GetParam(), &value, &encoded.value));
		owned_event result;

		ASSERT_EQ(COT_STATUS_OK,
			decode(GetParam(), bytes(encoded.value), result));
		ASSERT_NE(nullptr, result.get());
		EXPECT_EQ(present, result.get()->detail != nullptr);
		if (!present)
			continue;
		ASSERT_NE(nullptr, result.get()->detail);
		const cot_detail *view = result.get()->detail;

		EXPECT_EQ("", string(view->xml));
		EXPECT_EQ(nullptr, view->contact);
		EXPECT_EQ(nullptr, view->group);
		EXPECT_EQ(nullptr, view->precision_location);
		EXPECT_EQ(nullptr, view->status);
		EXPECT_EQ(nullptr, view->takv);
		EXPECT_EQ(nullptr, view->track);
		EXPECT_EQ(0u, view->extension_count);
	}
}

INSTANTIATE_TEST_SUITE_P(BothFormats, event_presence,
	testing::Values(COT_FORMAT_XML, COT_FORMAT_PROTOBUF));

TEST(EventXml, SelfClosingDetailIsPresent)
{
	owned_event result;
	std::string input = replace(document(), "</event>",
		"<detail/></event>");

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, input, result));
	ASSERT_NE(nullptr, result.get());
	ASSERT_NE(nullptr, result.get()->detail);
	EXPECT_EQ("", string(result.get()->detail->xml));
}

TEST(EventWire, SkipsUnknownFieldsWithoutLosingRepresentedFields)
{
	std::vector<uint8_t> fields = core_fields(event());

	append(fields, wire_uint(99, 123));
	append(fields, wire_double(100, 1.25));
	append(fields, wire_string(101, std::string("\xff\0", 2)));
	std::vector<uint8_t> input = protobuf_event(fields);

	append(input, wire_message(55, {1, 2, 3}));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF, input, result));
	ASSERT_NE(nullptr, result.get());
	expect_core(event(), *result.get());
}

TEST(EventWire, WrongWireTypeForKnownScalarIsAnUnknownField)
{
	std::vector<uint8_t> fields = core_fields(event());

	append(fields, wire_uint(5, 123));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		protobuf_event(fields), result));
	ASSERT_NE(nullptr, result.get());
	EXPECT_EQ("alpha-1", string(result.get()->uid));
}

TEST(EventText, UsesCountedBytesWithoutATerminator)
{
	cot_event value = event();
	const char uid[] = {'u', 'i', 'd', '\xc3', '\xa9', 'x', '\xff'};

	value.uid = {uid, 5};
	for (cot_format format : {COT_FORMAT_XML, COT_FORMAT_PROTOBUF}) {
		SCOPED_TRACE(format);
		owned_buffer encoded;

		ASSERT_EQ(COT_STATUS_OK,
			cot_encode(format, &value, &encoded.value));
		owned_event result;

		ASSERT_EQ(COT_STATUS_OK,
			decode(format, bytes(encoded.value), result));
		ASSERT_NE(nullptr, result.get());
		EXPECT_EQ(std::string(uid, 5), string(result.get()->uid));
	}
}

struct xml_text_case {
	const char *name;
	std::string value;
};

void PrintTo(const xml_text_case &value, std::ostream *output)
{
	*output << value.name;
}

class xml_inexpressible_text : public testing::TestWithParam<xml_text_case> {};

TEST_P(xml_inexpressible_text, SurvivesProtobufButCannotBeXml)
{
	cot_event value = event();
	const std::string input = GetParam().value;

	value.uid = text(input);
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &value, &encoded.value));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(encoded.value), result));
	ASSERT_NE(nullptr, result.get());
	const std::string actual = string(result.get()->uid);
	const std::vector<uint8_t> expected_bytes(input.begin(), input.end());
	const std::vector<uint8_t> actual_bytes(actual.begin(), actual.end());

	EXPECT_EQ(expected_bytes, actual_bytes);
	owned_buffer output;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, result.get(), &output.value));
	EXPECT_EQ(nullptr, output.value.data);
	EXPECT_EQ(0u, output.value.size);
}

INSTANTIATE_TEST_SUITE_P(ValidUtf8, xml_inexpressible_text, testing::Values(
	xml_text_case{"EmbeddedNul", std::string("a\0b", 3)},
	xml_text_case{"XmlControl", std::string("a\x01" "b", 3)},
	xml_text_case{"Noncharacter", std::string("\xef\xbf\xbe", 3)},
	xml_text_case{"EmptyRequiredText", std::string()}));

class required_event_text : public testing::TestWithParam<
	cot_text cot_event::*> {};

TEST_P(required_event_text, EmptyCoreStringsAreProtobufOnly)
{
	cot_event value = event();

	value.*GetParam() = {};
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &value, &encoded.value));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(encoded.value), result));
	ASSERT_NE(nullptr, result.get());
	expect_core(value, *result.get());
	owned_buffer output;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, result.get(), &output.value));
	EXPECT_EQ(nullptr, output.value.data);
	EXPECT_EQ(0u, output.value.size);
}

INSTANTIATE_TEST_SUITE_P(RequiredFields, required_event_text,
	testing::Values(&cot_event::type, &cot_event::uid, &cot_event::how));

TEST(EventCore, UnfamiliarTypeAndHowCodesRemainOpaque)
{
	cot_event value = event();
	const std::string type = "future-vendor-code";
	const std::string how = "not-in-a-known-enumeration";

	value.type = text(type);
	value.how = text(how);
	for (cot_format format : {COT_FORMAT_XML, COT_FORMAT_PROTOBUF}) {
		SCOPED_TRACE(format);
		owned_buffer encoded;

		ASSERT_EQ(COT_STATUS_OK,
			cot_encode(format, &value, &encoded.value));
		owned_event result;

		ASSERT_EQ(COT_STATUS_OK,
			decode(format, bytes(encoded.value), result));
		ASSERT_NE(nullptr, result.get());
		EXPECT_EQ(type, string(result.get()->type));
		EXPECT_EQ(how, string(result.get()->how));
	}
}

class access_default : public testing::TestWithParam<std::string> {};

TEST_P(access_default, XmlDefaultNormalizesAndIsOmittedOnOutput)
{
	std::string input = replace(document(), "version=\"2.0\"",
		"version=\"2.0\"" + GetParam());
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, input, parsed));
	ASSERT_NE(nullptr, parsed.get());
	EXPECT_EQ("", string(parsed.get()->access));
	owned_buffer wire;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, parsed.get(), &wire.value));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(wire.value), result));
	ASSERT_NE(nullptr, result.get());
	EXPECT_EQ("", string(result.get()->access));
	owned_buffer output;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, result.get(), &output.value));
	EXPECT_EQ(std::string::npos, bytes(output.value).find("access="));
}

INSTANTIATE_TEST_SUITE_P(CoTDefaults, access_default,
	testing::Values("", " access=\"\"", " access=\"Undefined\""));

TEST(EventCore, CallerUndefinedAccessNormalizesDuringProtobufEncoding)
{
	cot_event value = event();
	const std::string undefined = "Undefined";

	value.access = text(undefined);
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &value, &encoded.value));
	EXPECT_EQ(std::string::npos, bytes(encoded.value).find(undefined));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(encoded.value), result));
	ASSERT_NE(nullptr, result.get());
	EXPECT_EQ("", string(result.get()->access));
}

TEST(EventXml, OmitsEmptyOptionalEventAttributes)
{
	cot_event value = event();
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, &value, &encoded.value));
	for (const char *attribute : {"access=", "qos=", "opex=", "caveat=",
	     "releasableTo="})
		EXPECT_EQ(std::string::npos,
			bytes(encoded.value).find(attribute));
}

struct timestamp_case {
	uint64_t milliseconds;
	const char *text;
};

class xml_timestamp : public testing::TestWithParam<timestamp_case> {};

TEST_P(xml_timestamp, FormatsCanonicalUtcAndPreservesAllThreeFields)
{
	cot_event value = event();

	value.send_time_ms = GetParam().milliseconds;
	value.start_time_ms = GetParam().milliseconds;
	value.stale_time_ms = GetParam().milliseconds;
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, &value, &encoded.value));
	std::string output = bytes(encoded.value);
	size_t at = 0;
	size_t count = 0;

	while ((at = output.find(GetParam().text, at)) != std::string::npos) {
		++count;
		at += std::string(GetParam().text).size();
	}
	EXPECT_EQ(3u, count);
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, output, result));
	ASSERT_NE(nullptr, result.get());
	expect_core(value, *result.get());
}

INSTANTIATE_TEST_SUITE_P(Boundaries, xml_timestamp, testing::Values(
	timestamp_case{0, "1970-01-01T00:00:00.000Z"},
	timestamp_case{1, "1970-01-01T00:00:00.001Z"},
	timestamp_case{999, "1970-01-01T00:00:00.999Z"},
	timestamp_case{1000, "1970-01-01T00:00:01.000Z"},
	timestamp_case{951782400000, "2000-02-29T00:00:00.000Z"},
	timestamp_case{1704067200123, "2024-01-01T00:00:00.123Z"},
	timestamp_case{253402300799999, "9999-12-31T23:59:59.999Z"}));

struct parsed_timestamp {
	const char *text;
	uint64_t milliseconds;
};

class parsed_xml_timestamp : public testing::TestWithParam<parsed_timestamp> {};

TEST_P(parsed_xml_timestamp, NormalizesOffsetsAndExactPrecision)
{
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, document(GetParam().text), result));
	ASSERT_NE(nullptr, result.get());
	EXPECT_EQ(GetParam().milliseconds, result.get()->send_time_ms);
	EXPECT_EQ(GetParam().milliseconds, result.get()->start_time_ms);
	EXPECT_EQ(GetParam().milliseconds, result.get()->stale_time_ms);
}

INSTANTIATE_TEST_SUITE_P(Rfc3339, parsed_xml_timestamp, testing::Values(
	parsed_timestamp{"1970-01-01T00:00:00Z", 0},
	parsed_timestamp{"1970-01-01T01:00:00+01:00", 0},
	parsed_timestamp{"1969-12-31T23:00:00-01:00", 0},
	parsed_timestamp{"2024-01-01T01:00:00.123+01:00", 1704067200123},
	parsed_timestamp{"2023-12-31T18:30:00.123-05:30", 1704067200123},
	parsed_timestamp{"2024-01-01T00:00:00.1Z", 1704067200100},
	parsed_timestamp{"2024-01-01T00:00:00.12Z", 1704067200120},
	parsed_timestamp{"2024-01-01T00:00:00.123000Z", 1704067200123},
	parsed_timestamp{"2024-01-01T00:00:00.000000000Z", 1704067200000}));

struct bad_timestamp {
	const char *text;
	cot_status status;
};

class rejected_xml_timestamp : public testing::TestWithParam<bad_timestamp> {};

TEST_P(rejected_xml_timestamp, DistinguishesInvalidDatesFromUnsupportedValues)
{
	owned_event result;

	EXPECT_EQ(GetParam().status,
		decode(COT_FORMAT_XML, document(GetParam().text), result));
	EXPECT_EQ(nullptr, result.value);
}

INSTANTIATE_TEST_SUITE_P(Rfc3339, rejected_xml_timestamp, testing::Values(
	bad_timestamp{"1969-12-31T23:59:59.999Z",
		COT_STATUS_UNSUPPORTED_CONTENT},
	bad_timestamp{"9999-12-31T23:59:59.999-00:01",
		COT_STATUS_UNSUPPORTED_CONTENT},
	bad_timestamp{"2016-12-31T23:59:60Z", COT_STATUS_UNSUPPORTED_CONTENT},
	bad_timestamp{"2024-01-01T00:00:00.123001Z",
		COT_STATUS_UNSUPPORTED_CONTENT},
	bad_timestamp{"2024-01-01T00:00:00.000000001Z",
		COT_STATUS_UNSUPPORTED_CONTENT},
	bad_timestamp{"2024-02-30T00:00:00Z", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2100-02-29T00:00:00Z", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-13-01T00:00:00Z", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-00-01T00:00:00Z", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-01-00T00:00:00Z", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-01-01T24:00:00Z", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-01-01T00:60:00Z", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-01-01T00:00:00+24:00",
		COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-01-01T00:00:00+01:60",
		COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-01-01T00:00:00", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"2024-01-01", COT_STATUS_MALFORMED_MESSAGE},
	bad_timestamp{"not-a-time", COT_STATUS_MALFORMED_MESSAGE}));

class protobuf_timestamp : public testing::TestWithParam<uint64_t> {};

TEST_P(protobuf_timestamp, PreservesFullUnsignedDomain)
{
	cot_event value = event();

	value.send_time_ms = GetParam();
	value.start_time_ms = GetParam();
	value.stale_time_ms = GetParam();
	owned_event fixture;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		protobuf_event(core_fields(value)), fixture));
	ASSERT_NE(nullptr, fixture.get());
	expect_core(value, *fixture.get());
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &value, &encoded.value));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(encoded.value), result));
	ASSERT_NE(nullptr, result.get());
	expect_core(value, *result.get());
	if (GetParam() <= 253402300799999ULL)
		return;
	owned_buffer output;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, result.get(), &output.value));
	EXPECT_EQ(nullptr, output.value.data);
	EXPECT_EQ(0u, output.value.size);
}

INSTANTIATE_TEST_SUITE_P(Boundaries, protobuf_timestamp, testing::Values(
	uint64_t{0}, uint64_t{1}, uint64_t{253402300799999ULL},
	uint64_t{253402300800000ULL}, uint64_t{9223372036854775808ULL},
	std::numeric_limits<uint64_t>::max()));

class unrepresentable_timestamp : public testing::TestWithParam<
	uint64_t cot_event::*> {};

TEST_P(unrepresentable_timestamp, ValidatesEachTimestampForXmlOutput)
{
	cot_event value = event();

	value.*GetParam() = std::numeric_limits<uint64_t>::max();
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &value, &encoded.value));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(encoded.value), result));
	ASSERT_NE(nullptr, result.get());
	expect_core(value, *result.get());
	owned_buffer output;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, result.get(), &output.value));
	EXPECT_EQ(nullptr, output.value.data);
	EXPECT_EQ(0u, output.value.size);
}

INSTANTIATE_TEST_SUITE_P(EveryTimeField, unrepresentable_timestamp,
	testing::Values(&cot_event::send_time_ms, &cot_event::start_time_ms,
		&cot_event::stale_time_ms));

TEST(EventTime, HasNoOrderingExpiryOrImplicitClockConstraint)
{
	cot_event value = event();

	value.send_time_ms = 1000;
	value.start_time_ms = 2000;
	value.stale_time_ms = 0;
	for (cot_format format : {COT_FORMAT_XML, COT_FORMAT_PROTOBUF}) {
		SCOPED_TRACE(format);
		owned_buffer encoded;

		ASSERT_EQ(COT_STATUS_OK,
			cot_encode(format, &value, &encoded.value));
		owned_event result;

		ASSERT_EQ(COT_STATUS_OK,
			decode(format, bytes(encoded.value), result));
		ASSERT_NE(nullptr, result.get());
		expect_core(value, *result.get());
	}
}

struct point_case {
	cot_point point;
	bool xml_supported;
};

class event_point : public testing::TestWithParam<point_case> {};

TEST_P(event_point, ProtobufPreservesDoubleValues)
{
	cot_event value = event();

	value.point = GetParam().point;
	owned_event fixture;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		protobuf_event(core_fields(value)), fixture));
	ASSERT_NE(nullptr, fixture.get());
	expect_point(value.point, fixture.get()->point);
	owned_buffer encoded;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &value, &encoded.value));
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(encoded.value), result));
	ASSERT_NE(nullptr, result.get());
	expect_point(value.point, result.get()->point);
}

TEST_P(event_point, XmlPreservesRepresentableValuesAndRejectsOthers)
{
	cot_event value = event();

	value.point = GetParam().point;
	owned_buffer encoded;
	cot_status status = cot_encode(COT_FORMAT_XML, &value, &encoded.value);

	if (!GetParam().xml_supported) {
		EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT, status);
		EXPECT_EQ(nullptr, encoded.value.data);
		EXPECT_EQ(0u, encoded.value.size);
		return;
	}
	ASSERT_EQ(COT_STATUS_OK, status);
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(encoded.value), result));
	ASSERT_NE(nullptr, result.get());
	expect_point(value.point, result.get()->point);
}

const double infinity = std::numeric_limits<double>::infinity();
const double nan = std::numeric_limits<double>::quiet_NaN();
const double smallest = std::numeric_limits<double>::denorm_min();
const double largest = std::numeric_limits<double>::max();

INSTANTIATE_TEST_SUITE_P(Boundaries, event_point, testing::Values(
	point_case{{0, 0, 0, 0, 0}, true},
	point_case{{-0.0, -0.0, -0.0, -0.0, -0.0}, true},
	point_case{{90, 180, -25, 0, 999999}, true},
	point_case{{-90, -180, 999999, 999999, 999999}, true},
	point_case{{38.123456789012344, -77.98765432109876,
		-123.45678901234567, 0.12345678901234566, 1e-120}, true},
	point_case{{smallest, -smallest, largest, smallest, largest}, true},
	point_case{{90.00000000000001, 0, 0, 0, 0}, false},
	point_case{{-90.00000000000001, 0, 0, 0, 0}, false},
	point_case{{0, 180.00000000000003, 0, 0, 0}, false},
	point_case{{0, -180.00000000000003, 0, 0, 0}, false},
	point_case{{0, 0, 0, -smallest, 0}, false},
	point_case{{0, 0, 0, 0, -1}, false},
	point_case{{infinity, 0, 0, 0, 0}, false},
	point_case{{0, -infinity, 0, 0, 0}, false},
	point_case{{0, 0, nan, 0, 0}, false},
	point_case{{0, 0, infinity, 0, 0}, false},
	point_case{{0, 0, 0, nan, 0}, false},
	point_case{{0, 0, 0, 0, infinity}, false}));

struct xml_failure {
	std::string input;
	cot_status status;
};

class rejected_event_xml : public testing::TestWithParam<xml_failure> {};

TEST_P(rejected_event_xml, ReportsContractFailureAndLeavesNullOwner)
{
	owned_event result;

	EXPECT_EQ(GetParam().status,
		decode(COT_FORMAT_XML, GetParam().input, result));
	EXPECT_EQ(nullptr, result.value);
}

std::vector<xml_failure> xml_failures()
{
	std::vector<xml_failure> cases = {
		{"", COT_STATUS_MALFORMED_MESSAGE},
		{" ", COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "</event>", "</wrong>"),
			COT_STATUS_MALFORMED_MESSAGE},
		{document() + document(), COT_STATUS_MALFORMED_MESSAGE},
		{document() + "trailing", COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "<point lat=\"38\" lon=\"-77\" "
			"hae=\"10\" ce=\"2\" le=\"3\"/>", ""),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "</event>",
			"<point lat=\"0\" lon=\"0\" hae=\"0\" "
			"ce=\"0\" le=\"0\"/></event>"),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "</event>",
			"<detail/><detail/></event>"),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "type=\"a-f-G-U-C\"", "type=\"\""),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "uid=\"alpha-1\"", "uid=\"\""),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "how=\"m-g\"", "how=\"\""),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "lat=\"38\"", "lat=\"north\""),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "hae=\"10\"", "hae=\"1.2.3\""),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "le=\"3\"", "le=\"\""),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "alpha-1", std::string("\xc0\xaf", 2)),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "alpha-1", "a&undefined;b"),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "alpha-1", "a&#0;b"),
			COT_STATUS_MALFORMED_MESSAGE},
		{replace(document(), "version=\"2.0\"", "version=\"3.0\""),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "version=\"2.0\"",
			"version=\"2.0\" extra=\"value\""),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "lat=\"38\"", "lat=\"91\""),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "lon=\"-77\"", "lon=\"-181\""),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "ce=\"2\"", "ce=\"-1\""),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "le=\"3\"", "le=\"-1\""),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "lat=\"38\"",
			"lat=\"38\" extra=\"value\""),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "</event>", "<other/></event>"),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "le=\"3\"/>", "le=\"3\"><x/></point>"),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "</event>",
			"<detail extra=\"value\"/></event>"),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "<event ", "<event xmlns=\"urn:vendor\" "),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "<point ", "<point xmlns=\"urn:vendor\" "),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{replace(document(), "</event>",
			"<detail xmlns=\"urn:vendor\"/></event>"),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{"<!DOCTYPE event>" + document(),
			COT_STATUS_UNSUPPORTED_CONTENT},
		{"<!DOCTYPE event [<!ENTITY vendor 'value'>]>" + document(),
			COT_STATUS_UNSUPPORTED_CONTENT}
	};
	for (const char *attribute : {"version=\"2.0\" ",
	     "type=\"a-f-G-U-C\" ", "uid=\"alpha-1\" ", "how=\"m-g\" ",
	     "time=\"2024-01-01T00:00:00.123Z\" ",
	     "start=\"2024-01-01T00:00:00.123Z\" ",
	     " stale=\"2024-01-01T00:00:00.123Z\"", "lat=\"38\" ",
	     "lon=\"-77\" ", "hae=\"10\" ", "ce=\"2\" ", " le=\"3\""})
		cases.push_back({replace(document(), attribute, ""),
			COT_STATUS_MALFORMED_MESSAGE});
	return cases;
}

INSTANTIATE_TEST_SUITE_P(CoreValidation, rejected_event_xml,
	testing::ValuesIn(xml_failures()));

TEST(EventXml, AllowsDeclarationWhitespaceAndXmlEntities)
{
	std::string input = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" +
		replace(document(), "alpha-1",
			"A&amp;B&lt;&gt;&quot;&apos;&#233;");
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, input + "\n\t ", result));
	ASSERT_NE(nullptr, result.get());
	EXPECT_EQ(u8"A&B<>\"'é", string(result.get()->uid));
}

TEST(EventXml, RejectsTrailingCommentsAndProcessingInstructions)
{
	for (const char *trailing : {"<!--trailing-->", "<?trailing data?>"}) {
		SCOPED_TRACE(trailing);
		owned_event owner;

		EXPECT_EQ(COT_STATUS_MALFORMED_MESSAGE,
			decode(COT_FORMAT_XML, document() + trailing, owner));
		EXPECT_EQ(nullptr, owner.value);
	}
}

TEST(EventXml, AllowsSurroundingWhitespaceWithoutADeclaration)
{
	owned_event result;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, " \n" + document() + "\n\t ", result));
	ASSERT_NE(nullptr, result.get());
	EXPECT_EQ("alpha-1", string(result.get()->uid));
}

class rejected_envelope : public testing::TestWithParam<
	std::vector<uint8_t>> {};

TEST_P(rejected_envelope, RejectsUnsupportedContentWithNullOwner)
{
	owned_event result;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		decode(COT_FORMAT_PROTOBUF, GetParam(), result));
	EXPECT_EQ(nullptr, result.value);
}

std::vector<std::vector<uint8_t>> unsupported_envelopes()
{
	std::vector<uint8_t> mixed = protobuf_event(core_fields(event()));

	append(mixed, wire_message(1, wire_uint(1, 1)));
	std::vector<uint8_t> empty_control = protobuf_event({});

	append(empty_control, wire_message(1, {}));
	return {{}, wire_uint(90, 1), wire_message(1, {}),
		wire_message(1, wire_uint(1, 1)), mixed, empty_control};
}

INSTANTIATE_TEST_SUITE_P(TakMessage, rejected_envelope,
	testing::ValuesIn(unsupported_envelopes()));

class malformed_wire : public testing::TestWithParam<
	std::vector<uint8_t>> {};

TEST_P(malformed_wire, RejectsTruncationInvalidTagsAndInvalidUtf8)
{
	owned_event result;

	EXPECT_EQ(COT_STATUS_MALFORMED_MESSAGE,
		decode(COT_FORMAT_PROTOBUF, GetParam(), result));
	EXPECT_EQ(nullptr, result.value);
}

TEST_P(malformed_wire, WireFailureTakesPrecedenceOverControlContent)
{
	std::vector<uint8_t> input = wire_message(1, {});

	append(input, GetParam());
	owned_event result;

	EXPECT_EQ(COT_STATUS_MALFORMED_MESSAGE,
		decode(COT_FORMAT_PROTOBUF, input, result));
	EXPECT_EQ(nullptr, result.value);
}

INSTANTIATE_TEST_SUITE_P(Boundaries, malformed_wire, testing::Values(
	std::vector<uint8_t>{0},
	std::vector<uint8_t>{0x80},
	std::vector<uint8_t>{0x12},
	std::vector<uint8_t>{0x12, 0x80},
	std::vector<uint8_t>{0x12, 2, 0x0a},
	std::vector<uint8_t>{0x12, 1, 0},
	std::vector<uint8_t>{0x12, 1, 0x0e},
	std::vector<uint8_t>{0x12, 2, 0x30, 0x80},
	protobuf_event({0x51, 0, 0, 0}),
	protobuf_event({0x0a, 2, 'x'}),
	protobuf_event(wire_string(5, std::string("\xff", 1))),
	protobuf_event(wire_string(1, std::string("\xc0\xaf", 2))),
	protobuf_event(wire_string(9, std::string("\xed\xa0\x80", 3))),
	std::vector<uint8_t>{0x12, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0x02},
	protobuf_event({0x30, 0x80, 0x80, 0x80, 0x80, 0x80,
		0x80, 0x80, 0x80, 0x80, 0x80, 0})));

} /* namespace */
