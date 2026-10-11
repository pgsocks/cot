#include "support.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace cot_test;

enum class detail_kind {
	contact,
	group,
	precision,
	status,
	takv,
	track,
};

struct typed_details {
	cot_contact contact{};
	cot_group group{};
	cot_precision_location precision{};
	cot_status_detail status{};
	cot_takv takv{};
	cot_track track{};
};

cot_text literal(const char *value)
{
	return {value, std::strlen(value)};
}

std::string counted(cot_text value)
{
	if (!value.size)
		return {};
	return {value.data, value.size};
}

const char *element_name(detail_kind kind)
{
	switch (kind) {
	case detail_kind::contact:
		return "contact";
	case detail_kind::group:
		return "__group";
	case detail_kind::precision:
		return "precisionlocation";
	case detail_kind::status:
		return "status";
	case detail_kind::takv:
		return "takv";
	case detail_kind::track:
		return "track";
	}

	return "";
}

std::string attributes(detail_kind kind)
{
	switch (kind) {
	case detail_kind::contact:
		return "callsign='Alpha' endpoint='host:4242:tcp' "
			"altendpoints='alt:4242:udp'";
	case detail_kind::group:
		return "name='Blue' role='Team Member'";
	case detail_kind::precision:
		return "geopointsrc='GPS' altsrc='DTED'";
	case detail_kind::status:
		return "battery='4294967295'";
	case detail_kind::takv:
		return "device='device-a' platform='ATAK' os='Android' "
			"version='5.5.1'";
	case detail_kind::track:
		return "speed='-12.5' course='1080.25'";
	}

	return {};
}

std::string element(detail_kind kind)
{
	return "<" + std::string(element_name(kind)) + " " +
		attributes(kind) + "/>";
}

std::string missing_attribute_element(detail_kind kind)
{
	switch (kind) {
	case detail_kind::contact:
		return "<contact endpoint='host:4242:tcp'/>";
	case detail_kind::group:
		return "<__group name='Blue'/>";
	case detail_kind::precision:
		return "<precisionlocation geopointsrc='GPS'/>";
	case detail_kind::status:
		return "<status/>";
	case detail_kind::takv:
		return "<takv device='device-a' platform='ATAK' os='Android'/>";
	case detail_kind::track:
		return "<track speed='-12.5'/>";
	}

	return {};
}

cot_detail selected_detail(detail_kind kind, typed_details &values)
{
	cot_detail detail{};

	switch (kind) {
	case detail_kind::contact:
		detail.contact = &values.contact;
		break;
	case detail_kind::group:
		detail.group = &values.group;
		break;
	case detail_kind::precision:
		detail.precision_location = &values.precision;
		break;
	case detail_kind::status:
		detail.status = &values.status;
		break;
	case detail_kind::takv:
		detail.takv = &values.takv;
		break;
	case detail_kind::track:
		detail.track = &values.track;
		break;
	}

	return detail;
}

void populate(typed_details &values)
{
	values.contact = {literal("host:4242:tcp"), literal("Alpha"),
		literal("alt:4242:udp")};
	values.group = {literal("Blue"), literal("Team Member")};
	values.precision = {literal("GPS"), literal("DTED")};
	values.status.battery = UINT32_MAX;
	values.takv = {literal("device-a"), literal("ATAK"),
		literal("Android"), literal("5.5.1")};
	values.track = {-12.5, 1080.25};
}

void expect_presence(const cot_detail &value, detail_kind kind)
{
	EXPECT_EQ(value.contact != nullptr, kind == detail_kind::contact);
	EXPECT_EQ(value.group != nullptr, kind == detail_kind::group);
	EXPECT_EQ(value.precision_location != nullptr,
		kind == detail_kind::precision);
	EXPECT_EQ(value.status != nullptr, kind == detail_kind::status);
	EXPECT_EQ(value.takv != nullptr, kind == detail_kind::takv);
	EXPECT_EQ(value.track != nullptr, kind == detail_kind::track);
}

void expect_absent(const cot_detail &value)
{
	EXPECT_EQ(nullptr, value.contact);
	EXPECT_EQ(nullptr, value.group);
	EXPECT_EQ(nullptr, value.precision_location);
	EXPECT_EQ(nullptr, value.status);
	EXPECT_EQ(nullptr, value.takv);
	EXPECT_EQ(nullptr, value.track);
}

void expect_values(const cot_detail &actual, const cot_detail &expected,
	detail_kind kind)
{
	expect_presence(actual, kind);
	switch (kind) {
	case detail_kind::contact:
		ASSERT_NE(nullptr, actual.contact);
		EXPECT_EQ(counted(expected.contact->callsign),
			counted(actual.contact->callsign));
		EXPECT_EQ(counted(expected.contact->endpoint),
			counted(actual.contact->endpoint));
		EXPECT_EQ(counted(expected.contact->altendpoints),
			counted(actual.contact->altendpoints));
		break;
	case detail_kind::group:
		ASSERT_NE(nullptr, actual.group);
		EXPECT_EQ(counted(expected.group->name),
			counted(actual.group->name));
		EXPECT_EQ(counted(expected.group->role),
			counted(actual.group->role));
		break;
	case detail_kind::precision:
		ASSERT_NE(nullptr, actual.precision_location);
		EXPECT_EQ(counted(expected.precision_location->geopointsrc),
			counted(actual.precision_location->geopointsrc));
		EXPECT_EQ(counted(expected.precision_location->altsrc),
			counted(actual.precision_location->altsrc));
		break;
	case detail_kind::status:
		ASSERT_NE(nullptr, actual.status);
		EXPECT_EQ(expected.status->battery, actual.status->battery);
		break;
	case detail_kind::takv:
		ASSERT_NE(nullptr, actual.takv);
		EXPECT_EQ(counted(expected.takv->device),
			counted(actual.takv->device));
		EXPECT_EQ(counted(expected.takv->platform),
			counted(actual.takv->platform));
		EXPECT_EQ(counted(expected.takv->os),
			counted(actual.takv->os));
		EXPECT_EQ(counted(expected.takv->version),
			counted(actual.takv->version));
		break;
	case detail_kind::track:
		ASSERT_NE(nullptr, actual.track);
		EXPECT_DOUBLE_EQ(expected.track->speed, actual.track->speed);
		EXPECT_DOUBLE_EQ(expected.track->course, actual.track->course);
		break;
	}
}

std::vector<uint8_t> detail_message(const std::vector<uint8_t> &fields)
{
	std::vector<uint8_t> event_fields = wire_string(1, "a-f-G-U-C");

	append(event_fields, wire_string(5, "alpha-1"));
	append(event_fields, wire_string(9, "m-g"));
	append(event_fields, wire_message(15, fields));
	return protobuf_event(event_fields);
}

class detail_mapping : public testing::TestWithParam<detail_kind> {
};

TEST_P(detail_mapping, MapsTheWholeXmlElement)
{
	typed_details expected_values;

	populate(expected_values);
	cot_detail expected = selected_detail(GetParam(), expected_values);
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, xml(element(GetParam())), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_values(*parsed.get()->detail, expected, GetParam());
	EXPECT_TRUE(counted(parsed.get()->detail->xml).empty());
}

TEST_P(detail_mapping, CallerValuesSurviveProtobufAndXmlConversion)
{
	typed_details values;

	populate(values);
	cot_detail detail = selected_detail(GetParam(), values);
	cot_event source = event();

	source.detail = &detail;
	owned_buffer protobuf;
	owned_event parsed;
	owned_buffer document;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &source, &protobuf.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(protobuf.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_values(*parsed.get()->detail, detail, GetParam());
	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	expect_values(*reparsed.get()->detail, detail, GetParam());
}

TEST_P(detail_mapping, PreservesPresentDefaultValuedMessages)
{
	typed_details values;
	cot_detail detail = selected_detail(GetParam(), values);
	cot_event source = event();

	source.detail = &detail;
	owned_buffer protobuf;
	owned_event parsed;
	owned_buffer document;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &source, &protobuf.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(protobuf.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_values(*parsed.get()->detail, detail, GetParam());
	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	expect_values(*reparsed.get()->detail, detail, GetParam());
}

TEST_P(detail_mapping, AcceptsFormattingWhitespaceInsideMappedElements)
{
	std::string name = element_name(GetParam());
	std::string child = "<" + name + " " + attributes(GetParam()) +
		"> \t\r\n </" + name + ">";
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_presence(*parsed.get()->detail, GetParam());
	EXPECT_TRUE(counted(parsed.get()->detail->xml).empty());
}

TEST_P(detail_mapping, KeepsExtraAttributesInWholeResidualElements)
{
	std::string child = "<" + std::string(element_name(GetParam())) +
		" " + attributes(GetParam()) + " custom='retained-marker'/>";
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	std::string residual = counted(parsed.get()->detail->xml);

	EXPECT_NE(std::string::npos, residual.find("retained-marker"));
	EXPECT_NE(std::string::npos, residual.find(element_name(GetParam())));
	owned_buffer document;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	expect_absent(*reparsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(reparsed.get()->detail->xml).find("retained-marker"));
}

TEST_P(detail_mapping, KeepsChildElementsInWholeResidualElements)
{
	std::string name = element_name(GetParam());
	std::string child = "<" + name + " " + attributes(GetParam()) +
		"><extra value='retained-marker'/></" + name + ">";
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(parsed.get()->detail->xml).find("retained-marker"));
}

TEST_P(detail_mapping, KeepsTextChildrenInWholeResidualElements)
{
	std::string name = element_name(GetParam());
	std::string child = "<" + name + " " + attributes(GetParam()) +
		">retained-marker</" + name + ">";
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(parsed.get()->detail->xml).find("retained-marker"));
}

TEST_P(detail_mapping, KeepsCommentChildrenInWholeResidualElements)
{
	std::string name = element_name(GetParam());
	std::string child = "<" + name + " " + attributes(GetParam()) +
		"><!--retained-marker--></" + name + ">";
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(parsed.get()->detail->xml).find("retained-marker"));
}

TEST_P(detail_mapping, MissingRequiredAttributesUseWholeElementFallback)
{
	owned_event parsed;
	std::string child = missing_attribute_element(GetParam());

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(parsed.get()->detail->xml).find(
			element_name(GetParam())));
}

TEST_P(detail_mapping, DuplicateElementsAllRemainResidual)
{
	std::string child = element(GetParam());
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, xml(child + child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	std::string residual = counted(parsed.get()->detail->xml);
	std::string opening = "<" + std::string(element_name(GetParam()));
	size_t first = residual.find(opening);

	ASSERT_NE(std::string::npos, first);
	EXPECT_NE(std::string::npos, residual.find(opening, first + 1));
}

TEST_P(detail_mapping, NamespacedElementsRemainResidual)
{
	std::string name = element_name(GetParam());
	std::string child = "<x:" + name + " xmlns:x='urn:cot:tests' " +
		attributes(GetParam()) + "/>";
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(parsed.get()->detail->xml).find("urn:cot:tests"));
	owned_buffer document;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	expect_absent(*reparsed.get()->detail);
}

TEST_P(detail_mapping, DefaultNamespacedElementsRemainResidual)
{
	std::string child = "<" + std::string(element_name(GetParam())) +
		" xmlns='urn:cot:tests' " + attributes(GetParam()) + "/>";
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(child), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(parsed.get()->detail->xml).find("urn:cot:tests"));
}

TEST_P(detail_mapping, ResidualTakesPrecedenceWithoutDiscardingTypedValues)
{
	typed_details values;
	cot_detail detail = selected_detail(GetParam(), values);
	std::string residual = element(GetParam());

	detail.xml = text(residual);
	cot_event source = event();

	source.detail = &detail;
	owned_buffer protobuf;
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &source, &protobuf.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(protobuf.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	EXPECT_EQ(residual, counted(parsed.get()->detail->xml));
	expect_values(*parsed.get()->detail, detail, GetParam());
	owned_buffer document;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	typed_details residual_values;

	populate(residual_values);
	cot_detail expected = selected_detail(GetParam(), residual_values);

	expect_values(*reparsed.get()->detail, expected, GetParam());
	EXPECT_TRUE(counted(reparsed.get()->detail->xml).empty());
	EXPECT_EQ(residual, counted(parsed.get()->detail->xml));
	expect_values(*parsed.get()->detail, detail, GetParam());
}

INSTANTIATE_TEST_SUITE_P(StandardDetails, detail_mapping,
	testing::Values(detail_kind::contact, detail_kind::group,
		detail_kind::precision, detail_kind::status, detail_kind::takv,
		detail_kind::track),
	[](const testing::TestParamInfo<detail_kind> &info) {
		switch (info.param) {
		case detail_kind::contact:
			return "Contact";
		case detail_kind::group:
			return "Group";
		case detail_kind::precision:
			return "PrecisionLocation";
		case detail_kind::status:
			return "Status";
		case detail_kind::takv:
			return "Takv";
		case detail_kind::track:
			return "Track";
		}
		return "Unknown";
	});

TEST(Details, AllMappingsCoexistWithMixedResidualXml)
{
	std::string detail = "before<custom value='residual-marker'/>between";

	for (detail_kind kind : {detail_kind::contact, detail_kind::group,
		detail_kind::precision, detail_kind::status, detail_kind::takv,
		detail_kind::track})
		detail += element(kind);
	detail += "after";
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, xml(detail), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	const cot_detail &view = *parsed.get()->detail;

	ASSERT_NE(nullptr, view.contact);
	ASSERT_NE(nullptr, view.group);
	ASSERT_NE(nullptr, view.precision_location);
	ASSERT_NE(nullptr, view.status);
	ASSERT_NE(nullptr, view.takv);
	ASSERT_NE(nullptr, view.track);
	std::string residual = counted(view.xml);

	EXPECT_NE(std::string::npos, residual.find("before"));
	EXPECT_NE(std::string::npos, residual.find("between"));
	EXPECT_NE(std::string::npos, residual.find("after"));
	EXPECT_NE(std::string::npos, residual.find("residual-marker"));
	EXPECT_EQ(std::string::npos, residual.find("<contact"));
	EXPECT_EQ(std::string::npos, residual.find("<__group"));
	EXPECT_EQ(std::string::npos, residual.find("<precisionlocation"));
	EXPECT_EQ(std::string::npos, residual.find("<status"));
	EXPECT_EQ(std::string::npos, residual.find("<takv"));
	EXPECT_EQ(std::string::npos, residual.find("<track"));
}

TEST(Details, FallbackDoesNotPreventOtherElementsFromMapping)
{
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML,
		xml("<contact callsign='Alpha' extra='retained-marker'/>"
			"<status battery='0'/>"), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	EXPECT_EQ(nullptr, parsed.get()->detail->contact);
	ASSERT_NE(nullptr, parsed.get()->detail->status);
	EXPECT_EQ(0U, parsed.get()->detail->status->battery);
	EXPECT_NE(std::string::npos,
		counted(parsed.get()->detail->xml).find("retained-marker"));
}

TEST(Details, EmptyOptionalContactAttributesCollapseToAbsence)
{
	owned_event parsed;
	std::string document =
		xml("<contact callsign='' endpoint='' altendpoints=''/>");

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, document, parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	ASSERT_NE(nullptr, parsed.get()->detail->contact);
	EXPECT_EQ(0U, parsed.get()->detail->contact->callsign.size);
	EXPECT_EQ(0U, parsed.get()->detail->contact->endpoint.size);
	EXPECT_EQ(0U, parsed.get()->detail->contact->altendpoints.size);
}

class unmappable_detail : public testing::TestWithParam<const char *> {
};

TEST_P(unmappable_detail, FallsBackWithoutFailingTheEvent)
{
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, xml(GetParam()), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_FALSE(counted(parsed.get()->detail->xml).empty());
}

INSTANTIATE_TEST_SUITE_P(UnmappableValues, unmappable_detail,
	testing::Values("<status battery='-1'/>",
		"<status battery='4294967296'/>",
		"<status battery='not-an-integer'/>",
		"<status battery='1.5'/>",
		"<track speed='bad' course='1'/>",
		"<track speed='1' course='bad'/>",
		"<track speed='NaN' course='1'/>",
		"<track speed='1' course='INF'/>"));

class missing_detail_attribute :
	public testing::TestWithParam<const char *> {
};

TEST_P(missing_detail_attribute, KeepsTheWholeElementResidual)
{
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, xml(GetParam()), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_FALSE(counted(parsed.get()->detail->xml).empty());
}

INSTANTIATE_TEST_SUITE_P(OtherRequiredAttributes, missing_detail_attribute,
	testing::Values("<__group role='Team Member'/>",
		"<precisionlocation altsrc='DTED'/>",
		"<takv platform='ATAK' os='Android' version='5.5.1'/>",
		"<takv device='device-a' os='Android' version='5.5.1'/>",
		"<takv device='device-a' platform='ATAK' version='5.5.1'/>",
		"<track course='180'/>"));

TEST(Details, RetainsInheritedNamespacesUsedOnlyInAttributeValues)
{
	std::string input = xml("<custom type='vendor:Thing'/>");

	input.insert(input.find("version="),
		"xmlns:vendor='urn:cot:qname-only' ");
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, input, parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	std::string residual = counted(parsed.get()->detail->xml);

	EXPECT_NE(std::string::npos, residual.find("urn:cot:qname-only"));
	EXPECT_NE(std::string::npos, residual.find("vendor:Thing"));
	owned_buffer output;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &output.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(output.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(reparsed.get()->detail->xml)
			.find("urn:cot:qname-only"));
}

TEST(Details, RetainsInheritedNamespacesNeededByResidualChildren)
{
	std::string document = xml("<x:extension value='residual-marker'/>");
	size_t position = document.find("<detail>");

	ASSERT_NE(std::string::npos, position);
	document.replace(position, std::strlen("<detail>"),
		"<detail xmlns:x='urn:cot:inherited'>");
	owned_event parsed;
	owned_buffer encoded;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_XML, document, parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(parsed.get()->detail->xml).find("urn:cot:inherited"));
	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &encoded.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(encoded.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	EXPECT_NE(std::string::npos,
		counted(reparsed.get()->detail->xml).find("residual-marker"));
}

TEST(Details, ResidualTrackSuppressesUnrepresentableTypedTrack)
{
	cot_track track{std::numeric_limits<double>::infinity(),
		std::numeric_limits<double>::quiet_NaN()};
	cot_detail detail{};

	detail.track = &track;
	detail.xml = literal("<track speed='-1.25' course='721.5'/>");
	cot_event source = event();

	source.detail = &detail;
	owned_buffer protobuf;
	owned_event parsed;
	owned_buffer document;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &source, &protobuf.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(protobuf.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	ASSERT_NE(nullptr, parsed.get()->detail->track);
	EXPECT_TRUE(std::isinf(parsed.get()->detail->track->speed));
	EXPECT_TRUE(std::isnan(parsed.get()->detail->track->course));
	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	ASSERT_NE(nullptr, reparsed.get()->detail->track);
	EXPECT_DOUBLE_EQ(-1.25, reparsed.get()->detail->track->speed);
	EXPECT_DOUBLE_EQ(721.5, reparsed.get()->detail->track->course);
}

TEST(Details, ResidualContactSuppressesXmlUnrepresentableTypedString)
{
	std::string callsign("A\0B", 3);
	cot_contact contact{};

	contact.callsign = text(callsign);
	cot_detail detail{};

	detail.contact = &contact;
	detail.xml = literal("<contact callsign='residual-marker'/>");
	cot_event source = event();

	source.detail = &detail;
	owned_buffer document;
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, &source, &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	ASSERT_NE(nullptr, parsed.get()->detail->contact);
	EXPECT_EQ("residual-marker",
		counted(parsed.get()->detail->contact->callsign));
}

class nonfinite_track : public testing::TestWithParam<unsigned> {
};

TEST_P(nonfinite_track, ProtobufRetainsValuesThatXmlCannotRepresent)
{
	double unusual = std::numeric_limits<double>::infinity();

	if (GetParam() % 3 == 1)
		unusual = -unusual;
	else if (GetParam() % 3 == 2)
		unusual = std::numeric_limits<double>::quiet_NaN();
	double speed = GetParam() < 3 ? unusual : 0.0;
	double course = GetParam() < 3 ? 0.0 : unusual;
	std::vector<uint8_t> fields = wire_double(1, speed);

	append(fields, wire_double(2, course));
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		detail_message(wire_message(7, fields)), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	ASSERT_NE(nullptr, parsed.get()->detail->track);
	owned_buffer protobuf;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, parsed.get(), &protobuf.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(protobuf.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	ASSERT_NE(nullptr, reparsed.get()->detail->track);
	const cot_track &track = *reparsed.get()->detail->track;
	double actual = GetParam() < 3 ? track.speed : track.course;

	if (std::isnan(unusual)) {
		EXPECT_TRUE(std::isnan(actual));
	} else {
		EXPECT_TRUE(std::isinf(actual));
		EXPECT_EQ(std::signbit(unusual), std::signbit(actual));
	}
	EXPECT_DOUBLE_EQ(0.0, GetParam() < 3 ? track.course : track.speed);
	owned_buffer document;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, reparsed.get(), &document.value));
	EXPECT_EQ(nullptr, document.value.data);
	EXPECT_EQ(0U, document.value.size);
}

INSTANTIATE_TEST_SUITE_P(EachNonfiniteTrackField, nonfinite_track,
	testing::Values(0U, 1U, 2U, 3U, 4U, 5U));

TEST(Details, XmlSpecialCharactersSurviveTypedStringConversion)
{
	std::string callsign = "A & < > \" ' \xc3\xa9";
	cot_contact contact{};

	contact.callsign = text(callsign);
	cot_detail detail{};

	detail.contact = &contact;
	cot_event source = event();

	source.detail = &detail;
	owned_buffer document;
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, &source, &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	ASSERT_NE(nullptr, parsed.get()->detail->contact);
	EXPECT_EQ(callsign, counted(parsed.get()->detail->contact->callsign));
}

TEST(Details, PresentEmptyDetailSurvivesProtobufAndXml)
{
	cot_detail detail{};
	cot_event source = event();

	source.detail = &detail;
	owned_buffer protobuf;
	owned_event parsed;
	owned_buffer document;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &source, &protobuf.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(protobuf.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_absent(*parsed.get()->detail);
	EXPECT_EQ(0U, parsed.get()->detail->xml.size);
	EXPECT_EQ(0U, parsed.get()->detail->extension_count);
	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	expect_absent(*reparsed.get()->detail);
}

TEST(Details, ProtobufPreservesInvalidResidualXmlAsOpaqueText)
{
	std::string residual = "<unfinished>&not-defined;";
	cot_detail detail{};

	detail.xml = text(residual);
	cot_event source = event();

	source.detail = &detail;
	owned_buffer protobuf;
	owned_event parsed;
	owned_buffer reencoded;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &source, &protobuf.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(protobuf.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	EXPECT_EQ(residual, counted(parsed.get()->detail->xml));
	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, parsed.get(),
			&reencoded.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(reencoded.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	EXPECT_EQ(residual, counted(reparsed.get()->detail->xml));
	owned_buffer document;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, reparsed.get(), &document.value));
	EXPECT_EQ(nullptr, document.value.data);
	EXPECT_EQ(0U, document.value.size);
}

TEST(Details, ProtobufPreservesEmbeddedNulInResidualXml)
{
	std::string residual("before\0after", 12);
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		detail_message(wire_string(1, residual)), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	EXPECT_EQ(residual, counted(parsed.get()->detail->xml));
	owned_buffer document;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	EXPECT_EQ(nullptr, document.value.data);
	EXPECT_EQ(0U, document.value.size);
}

class invalid_residual : public testing::TestWithParam<const char *> {
};

TEST_P(invalid_residual, XmlEncodingRejectsTheFragmentWithoutOutput)
{
	cot_detail detail{};

	detail.xml = literal(GetParam());
	cot_event source = event();

	source.detail = &detail;
	owned_buffer document;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, &source, &document.value));
	EXPECT_EQ(nullptr, document.value.data);
	EXPECT_EQ(0U, document.value.size);
}

INSTANTIATE_TEST_SUITE_P(UnrepresentableFragments, invalid_residual,
	testing::Values("<unclosed>", "<a/><b>", "&undefined;",
		"<?xml version='1.0'?><a/>",
		"<!DOCTYPE detail SYSTEM 'file:///must-not-be-read'><a/>",
		"<x:undeclared/>", "\x01"));

TEST(Details, ProtobufUsesOfficialTypedFieldNumbers)
{
	std::vector<uint8_t> contact = wire_string(1, "endpoint-marker");

	append(contact, wire_string(2, "callsign-marker"));
	append(contact, wire_string(3, "alternate-marker"));
	std::vector<uint8_t> group = wire_string(1, "group-marker");

	append(group, wire_string(2, "role-marker"));
	std::vector<uint8_t> precision = wire_string(1, "point-marker");

	append(precision, wire_string(2, "alt-marker"));
	std::vector<uint8_t> takv = wire_string(1, "device-marker");

	append(takv, wire_string(2, "platform-marker"));
	append(takv, wire_string(3, "os-marker"));
	append(takv, wire_string(4, "version-marker"));
	std::vector<uint8_t> track = wire_double(1, -13.25);

	append(track, wire_double(2, 1440.5));
	std::vector<uint8_t> fields = wire_message(2, contact);

	append(fields, wire_message(3, group));
	append(fields, wire_message(4, precision));
	append(fields, wire_message(5, wire_uint(1, UINT32_MAX)));
	append(fields, wire_message(6, takv));
	append(fields, wire_message(7, track));
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, detail_message(fields), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	const cot_detail &detail = *parsed.get()->detail;

	ASSERT_NE(nullptr, detail.contact);
	EXPECT_EQ("endpoint-marker", counted(detail.contact->endpoint));
	EXPECT_EQ("callsign-marker", counted(detail.contact->callsign));
	EXPECT_EQ("alternate-marker", counted(detail.contact->altendpoints));
	ASSERT_NE(nullptr, detail.group);
	EXPECT_EQ("group-marker", counted(detail.group->name));
	EXPECT_EQ("role-marker", counted(detail.group->role));
	ASSERT_NE(nullptr, detail.precision_location);
	EXPECT_EQ("point-marker",
		counted(detail.precision_location->geopointsrc));
	EXPECT_EQ("alt-marker", counted(detail.precision_location->altsrc));
	ASSERT_NE(nullptr, detail.status);
	EXPECT_EQ(UINT32_MAX, detail.status->battery);
	ASSERT_NE(nullptr, detail.takv);
	EXPECT_EQ("device-marker", counted(detail.takv->device));
	EXPECT_EQ("platform-marker", counted(detail.takv->platform));
	EXPECT_EQ("os-marker", counted(detail.takv->os));
	EXPECT_EQ("version-marker", counted(detail.takv->version));
	ASSERT_NE(nullptr, detail.track);
	EXPECT_DOUBLE_EQ(-13.25, detail.track->speed);
	EXPECT_DOUBLE_EQ(1440.5, detail.track->course);
}

void expect_extensions(const cot_detail &detail,
	const std::vector<cot_extension> &expected)
{
	ASSERT_EQ(expected.size(), detail.extension_count);
	if (expected.empty())
		return;
	ASSERT_NE(nullptr, detail.extensions);
	for (size_t index = 0; index < expected.size(); ++index) {
		SCOPED_TRACE(index);
		const cot_extension &actual = detail.extensions[index];

		EXPECT_EQ(expected[index].id, actual.id);
		ASSERT_EQ(expected[index].data.size, actual.data.size);
		if (actual.data.size) {
			ASSERT_NE(nullptr, actual.data.data);
			EXPECT_EQ(0, std::memcmp(expected[index].data.data,
				actual.data.data, actual.data.size));
		}
	}
}

TEST(Extensions, PreservesOrderRepeatedAndZeroIdsAndEveryByteValue)
{
	std::vector<uint8_t> payload;

	for (unsigned value = 0; value <= 255; ++value)
		payload.push_back(static_cast<uint8_t>(value));
	uint8_t other[] = {0xff, 0x00, 0x80};
	std::vector<cot_extension> extensions = {
		{0, {nullptr, 0}},
		{42, {payload.data(), payload.size()}},
		{42, {other, sizeof(other)}},
		{UINT32_MAX, {nullptr, 0}},
	};
	cot_detail detail{};

	detail.extensions = extensions.data();
	detail.extension_count = extensions.size();
	cot_event source = event();

	source.detail = &detail;
	owned_buffer protobuf;
	owned_event parsed;
	owned_buffer reencoded;
	owned_event reparsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, &source, &protobuf.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(protobuf.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	expect_extensions(*parsed.get()->detail, extensions);
	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_PROTOBUF, parsed.get(),
			&reencoded.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, bytes(reencoded.value), reparsed));
	ASSERT_NE(nullptr, reparsed.get());
	ASSERT_NE(nullptr, reparsed.get()->detail);
	expect_extensions(*reparsed.get()->detail, extensions);
}

TEST(Extensions, DecodesEmptyExtensionEntriesAsPresent)
{
	std::vector<uint8_t> fields = wire_message(8, {});

	append(fields, wire_message(8, wire_uint(1, 7)));
	append(fields, wire_message(8, {}));
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_PROTOBUF, detail_message(fields), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	std::vector<cot_extension> expected = {
		{0, {nullptr, 0}}, {7, {nullptr, 0}}, {0, {nullptr, 0}},
	};

	expect_extensions(*parsed.get()->detail, expected);
}

TEST(Extensions, UsesOfficialExtensionIdAndDataFieldNumbers)
{
	std::string payload("\0\xff\x80\x01", 4);
	std::vector<uint8_t> extension = wire_uint(1, UINT32_MAX);

	append(extension, wire_string(2, payload));
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		detail_message(wire_message(8, extension)), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	std::vector<cot_extension> expected = {{UINT32_MAX,
		{reinterpret_cast<const uint8_t *>(payload.data()),
			payload.size()}}};

	expect_extensions(*parsed.get()->detail, expected);
}

TEST(Extensions, EvenAnEmptyZeroIdEntryHasNoXmlRepresentation)
{
	cot_extension extension{};
	cot_detail detail{};

	detail.extensions = &extension;
	detail.extension_count = 1;
	cot_event source = event();

	source.detail = &detail;
	owned_buffer document;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, &source, &document.value));
	EXPECT_EQ(nullptr, document.value.data);
	EXPECT_EQ(0U, document.value.size);
}

TEST(Extensions, ParsedEmptyEntryPreventsXmlConversion)
{
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK, decode(COT_FORMAT_PROTOBUF,
		detail_message(wire_message(8, {})), parsed));
	owned_buffer document;

	EXPECT_EQ(COT_STATUS_UNSUPPORTED_CONTENT,
		cot_encode(COT_FORMAT_XML, parsed.get(), &document.value));
	EXPECT_EQ(nullptr, document.value.data);
	EXPECT_EQ(0U, document.value.size);
}

TEST(Extensions, NonnullArrayWithZeroCountIsEmpty)
{
	cot_extension unused{};
	cot_detail detail{};

	detail.extensions = &unused;
	cot_event source = event();

	source.detail = &detail;
	owned_buffer document;
	owned_event parsed;

	ASSERT_EQ(COT_STATUS_OK,
		cot_encode(COT_FORMAT_XML, &source, &document.value));
	ASSERT_EQ(COT_STATUS_OK,
		decode(COT_FORMAT_XML, bytes(document.value), parsed));
	ASSERT_NE(nullptr, parsed.get());
	ASSERT_NE(nullptr, parsed.get()->detail);
	EXPECT_EQ(0U, parsed.get()->detail->extension_count);
}

} // namespace
