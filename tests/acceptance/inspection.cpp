#include "context.h"
#include <cot/inspect.hpp>

namespace
{

struct InspectionContext {
	cot_event event{};
	cot_detail detail{};
	cot_contact contact{};
	cot_field_value values[4]{};
	const cot_field_info *metadata = nullptr;
	cot_format format = COT_FORMAT_PROTOBUF;
	std::string input;
	cot_diagnostic diagnostic{};
	owned_event owner;
	cot_status status = COT_STATUS_OK;
};

GIVEN("^an event with Undefined access and a present empty contact$")
{
	ScenarioScope<InspectionContext> c;

	c->event.access = {"Undefined", 9};
	c->event.detail = &c->detail;
	c->detail.contact = &c->contact;
}

WHEN("^I inspect access, contact callsign, endpoint, and latitude$")
{
	ScenarioScope<InspectionContext> c;
	const char *const paths[] = {"access", "detail.contact.callsign",
		"detail.contact.endpoint", "point.latitude"};

	for (unsigned i = 0; i < 4; ++i)
		ASSERT_EQ(cot_field_get(&c->event, cot_field_resolve(paths[i]),
			&c->values[i]), COT_STATUS_OK);
}

THEN("^access and endpoint are unavailable mapping sources$")
{
	ScenarioScope<InspectionContext> c;

	EXPECT_FALSE(c->values[0].available);
	EXPECT_FALSE(c->values[2].available);
}

THEN("^the empty callsign and zero latitude are available mapping sources$")
{
	ScenarioScope<InspectionContext> c;

	EXPECT_TRUE(c->values[1].available);
	EXPECT_EQ(c->values[1].value.text.size, 0u);
	EXPECT_TRUE(c->values[3].available);
	EXPECT_EQ(c->values[3].value.real, 0);
}

THEN("^the contact group is present$")
{
	ScenarioScope<InspectionContext> c;
	int present = 0;

	ASSERT_EQ(cot_group_is_present(&c->event, COT_GROUP_CONTACT, &present),
		COT_STATUS_OK);
	EXPECT_EQ(present, 1);
}

GIVEN("^a Protobuf event with one unknown field$")
{
	ScenarioScope<InspectionContext> c;
	auto input = protobuf_event(wire_uint(100, 42));

	c->input.assign(input.begin(), input.end());
}

GIVEN("^an XML event missing its uid attribute$")
{
	ScenarioScope<InspectionContext> c;

	c->format = COT_FORMAT_XML;
	c->input = xml();
	replace(c->input, "uid='alpha-1' ", "");
}

WHEN("^I decode it with structured diagnostics$")
{
	ScenarioScope<InspectionContext> c;

	c->status = cot_decode_ex(c->format,
		reinterpret_cast<const uint8_t *>(c->input.data()),
		c->input.size(), &c->owner.value, &c->diagnostic);
}

THEN("^decoding succeeds and reports one discarded Protobuf field$")
{
	ScenarioScope<InspectionContext> c;

	EXPECT_EQ(c->status, COT_STATUS_OK);
	EXPECT_EQ(c->diagnostic.reason, COT_DIAGNOSTIC_NONE);
	EXPECT_EQ(c->diagnostic.unknown_protobuf_fields, 1u);
	EXPECT_EQ(c->diagnostic.unknown_protobuf_bytes, wire_uint(100, 42).size());
}

THEN("^the decoded semantic values can still be inspected$")
{
	ScenarioScope<InspectionContext> c;
	cot_field_value value{};

	ASSERT_EQ(cot_field_get(c->owner.get(), COT_FIELD_SEND_TIME_MS, &value),
		COT_STATUS_OK);
	EXPECT_TRUE(value.available);
	EXPECT_EQ(value.value.uint_value, 0u);
}

THEN("^decoding fails with a missing uid diagnostic and input location$")
{
	ScenarioScope<InspectionContext> c;

	EXPECT_EQ(c->status, COT_STATUS_MALFORMED_MESSAGE);
	EXPECT_EQ(c->owner.value, nullptr);
	EXPECT_EQ(c->diagnostic.reason, COT_DIAGNOSTIC_MISSING_REQUIRED);
	EXPECT_STREQ(c->diagnostic.path, "uid");
	EXPECT_EQ(c->diagnostic.byte_offset, c->input.find("<event"));
}

WHEN("^I inspect metadata for \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, path);
	ScenarioScope<InspectionContext> c;

	c->metadata = cot_field_get_info(cot_field_resolve(path.c_str()));
	ASSERT_NE(c->metadata, nullptr);
}

THEN("^its type is \"([^\"]*)\", unit is \"([^\"]*)\", and group is "
	"\"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, type);
	REGEX_PARAM(std::string, unit);
	REGEX_PARAM(std::string, group);
	ScenarioScope<InspectionContext> c;

	ASSERT_NE(c->metadata, nullptr);
	EXPECT_STREQ(cot_field_type_string(c->metadata->type), type.c_str());
	EXPECT_STREQ(cot_field_unit_string(c->metadata->unit), unit.c_str());
	EXPECT_STREQ(cot_group_id_string(c->metadata->group), group.c_str());
}

} // namespace
