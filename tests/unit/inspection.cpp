#include <cot/inspect.hpp>
#include <gtest/gtest.h>
#include "support.h"

#include <cstring>

extern "C" cot_status cot_test_c_inspect(const cot_event *, cot_field_value *);
extern "C" cot_status cot_test_c_field_id(int, const cot_event *,
	cot_field_value *);
extern "C" cot_status cot_test_c_group_id(int, const cot_event *, int *);

extern "C" const char *cot_test_c_field_type_string(int);
extern "C" const char *cot_test_c_field_unit_string(int);
extern "C" const char *cot_test_c_group_id_string(int);

namespace
{

TEST(Inspection, CatalogResolvesEveryCanonicalNativeField)
{
	ASSERT_EQ(cot_field_count(), 32u);
	for (size_t i = 1; i <= cot_field_count(); ++i) {
		auto id = static_cast<cot_field_id>(i);
		const cot_field_info *info = cot_field_get_info(id);

		ASSERT_NE(info, nullptr);
		EXPECT_EQ(info->id, id);
		EXPECT_EQ(cot_field_resolve(info->path), id);
	}
	EXPECT_EQ(cot_field_get_info(COT_FIELD_INVALID), nullptr);
	EXPECT_EQ(cot_field_resolve(nullptr), COT_FIELD_INVALID);
	EXPECT_EQ(cot_field_resolve("sendTime"), COT_FIELD_INVALID);
	EXPECT_EQ(cot_field_resolve("TYPE"), COT_FIELD_INVALID);
	EXPECT_EQ(cot_field_get_info(COT_FIELD_LATITUDE)->unit,
		COT_UNIT_DEGREES);
	EXPECT_EQ(cot_field_get_info(COT_FIELD_TRACK_SPEED)->unit,
		COT_UNIT_METRES_PER_SECOND);
}

TEST(Inspection, AvailabilityUsesCoTSemanticsRatherThanScalarDefaults)
{
	cot_event event{};
	cot_field_value value{};

	ASSERT_EQ(cot_field_get(&event, COT_FIELD_ACCESS, &value),
		COT_STATUS_OK);
	EXPECT_FALSE(value.available);
	event.access = {"Undefined", 9};
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_ACCESS, &value),
		COT_STATUS_OK);
	EXPECT_FALSE(value.available);
	event.qos = {"", 0};
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_QOS, &value),
		COT_STATUS_OK);
	EXPECT_FALSE(value.available);
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_TYPE, &value),
		COT_STATUS_OK);
	EXPECT_TRUE(value.available);
	EXPECT_EQ(value.value.text.size, 0u);
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_LATITUDE, &value),
		COT_STATUS_OK);
	EXPECT_TRUE(value.available);
	EXPECT_EQ(value.value.real, 0);
	event.send_time_ms = UINT64_MAX;
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_SEND_TIME_MS, &value),
		COT_STATUS_OK);
	EXPECT_TRUE(value.available);
	EXPECT_EQ(value.type, COT_VALUE_UINT64);
	EXPECT_EQ(value.value.uint_value, UINT64_MAX);
}

TEST(Inspection, EmptyNestedMessagesRetainRequiredChildren)
{
	cot_event event{};
	cot_detail detail{};
	cot_contact contact{};
	cot_status_detail status{};
	cot_track track{};
	cot_field_value value{};
	int present = 99;

	ASSERT_EQ(cot_group_is_present(&event, COT_GROUP_CONTACT, &present),
		COT_STATUS_OK);
	EXPECT_EQ(present, 0);
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_CONTACT_CALLSIGN, &value),
		COT_STATUS_OK);
	EXPECT_FALSE(value.available);
	event.detail = &detail;
	detail.contact = &contact;
	detail.status = &status;
	detail.track = &track;
	ASSERT_EQ(cot_group_is_present(&event, COT_GROUP_CONTACT, &present),
		COT_STATUS_OK);
	EXPECT_EQ(present, 1);
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_CONTACT_CALLSIGN, &value),
		COT_STATUS_OK);
	EXPECT_TRUE(value.available);
	EXPECT_EQ(value.value.text.size, 0u);
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_CONTACT_ENDPOINT, &value),
		COT_STATUS_OK);
	EXPECT_FALSE(value.available);
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_BATTERY, &value),
		COT_STATUS_OK);
	EXPECT_TRUE(value.available);
	EXPECT_EQ(value.type, COT_VALUE_UINT32);
	EXPECT_EQ(value.value.uint_value, 0u);
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_TRACK_SPEED, &value),
		COT_STATUS_OK);
	EXPECT_TRUE(value.available);
	EXPECT_EQ(value.value.real, 0);
}

TEST(Inspection, TextAndExtensionsBorrowExactNativeStorage)
{
	const char counted[] = {'A', 0, 'B'};
	cot_extension extension{0, {nullptr, 0}};
	cot_detail detail{};
	cot_event event{};
	cot_field_value value{};

	event.uid = {counted, sizeof(counted)};
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_UID, &value),
		COT_STATUS_OK);
	EXPECT_EQ(value.value.text.data, counted);
	EXPECT_EQ(value.value.text.size, sizeof(counted));
	event.detail = &detail;
	detail.extensions = &extension;
	detail.extension_count = 1;
	ASSERT_EQ(cot_field_get(&event, COT_FIELD_EXTENSIONS, &value),
		COT_STATUS_OK);
	EXPECT_TRUE(value.available);
	EXPECT_EQ(value.value.extensions.data, &extension);
	EXPECT_EQ(value.value.extensions.size, 1u);
}

TEST(Inspection, InvalidRequestsResetOutputs)
{
	cot_event event{};
	cot_field_value value{};
	int present = 1;

	value.available = 1;
	EXPECT_EQ(cot_field_get(nullptr, COT_FIELD_UID, &value),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(value.available, 0);
	EXPECT_EQ(cot_field_get(&event, COT_FIELD_INVALID, &value),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(cot_field_get(&event, COT_FIELD_UID, nullptr),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(cot_group_is_present(nullptr, COT_GROUP_EVENT, &present),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(present, 0);
}

TEST(Inspection, C11ConsumerUsesNativeTypesAndRejectsUnknownIds)
{
	cot_event event{};
	cot_field_value value{};

	event.send_time_ms = UINT64_MAX;
	ASSERT_EQ(cot_test_c_inspect(&event, &value), COT_STATUS_OK);
	EXPECT_EQ(value.value.uint_value, UINT64_MAX);
	for (int invalid : {-1, 999}) {
		int present = 1;

		EXPECT_EQ(cot_test_c_field_id(invalid, &event, &value),
			COT_STATUS_INVALID_ARGUMENT);
		EXPECT_EQ(value.available, 0);
		EXPECT_EQ(cot_test_c_group_id(invalid, &event, &present),
			COT_STATUS_INVALID_ARGUMENT);
		EXPECT_EQ(present, 0);
	}
}

TEST(Diagnostics, UnknownProtobufFieldsAreCountedWithoutKeepingWireStorage)
{
	auto fields = cot_test::wire_uint(100, 42);
	auto contact = cot_test::wire_uint(99, 3);
	auto detail = cot_test::wire_message(2, contact);

	cot_test::append(fields, cot_test::wire_message(15, detail));
	auto wire = cot_test::protobuf_event(fields);
	auto unknown = cot_test::wire_string(101, "ignored");

	cot_test::append(wire, unknown);
	cot_test::owned_event owner;
	cot_diagnostic diagnostic{};

	ASSERT_EQ(cot_decode_ex(COT_FORMAT_PROTOBUF, wire.data(), wire.size(),
		&owner.value, &diagnostic), COT_STATUS_OK);
	EXPECT_EQ(diagnostic.reason, COT_DIAGNOSTIC_NONE);
	EXPECT_EQ(diagnostic.byte_offset, COT_DIAGNOSTIC_NO_OFFSET);
	EXPECT_EQ(diagnostic.unknown_protobuf_fields, 3u);
	EXPECT_EQ(diagnostic.unknown_protobuf_bytes,
		cot_test::wire_uint(100, 42).size() + contact.size() +
		unknown.size());
}

TEST(Diagnostics, MalformedNestedWireIdentifiesFieldAndInputOffset)
{
	std::vector<uint8_t> wire{0x12, 0x03, 0x2a, 0x02, 'x'};
	cot_test::owned_event owner;
	cot_diagnostic diagnostic{};

	EXPECT_EQ(cot_decode_ex(COT_FORMAT_PROTOBUF, wire.data(), wire.size(),
		&owner.value, &diagnostic), COT_STATUS_MALFORMED_MESSAGE);
	EXPECT_EQ(owner.value, nullptr);
	EXPECT_EQ(diagnostic.reason, COT_DIAGNOSTIC_MALFORMED_PROTOBUF);
	ASSERT_NE(diagnostic.path, nullptr);
	EXPECT_STREQ(diagnostic.path, "uid");
	EXPECT_EQ(diagnostic.byte_offset, 2u);
}

TEST(Diagnostics, MissingXmlValueIdentifiesFieldAndOwningElement)
{
	std::string xml = cot_test::xml();
	size_t begin = xml.find("uid='alpha-1' ");

	ASSERT_NE(begin, std::string::npos);
	xml.erase(begin, std::strlen("uid='alpha-1' "));
	cot_test::owned_event owner;
	cot_diagnostic diagnostic{};

	EXPECT_EQ(cot_decode_ex(COT_FORMAT_XML,
		reinterpret_cast<const uint8_t *>(xml.data()), xml.size(),
		&owner.value, &diagnostic), COT_STATUS_MALFORMED_MESSAGE);
	EXPECT_EQ(diagnostic.reason, COT_DIAGNOSTIC_MISSING_REQUIRED);
	ASSERT_NE(diagnostic.path, nullptr);
	EXPECT_STREQ(diagnostic.path, "uid");
	EXPECT_EQ(diagnostic.byte_offset, xml.find("<event"));
}

TEST(Diagnostics, SyntaxAndArgumentsHaveStructuredReasons)
{
	const uint8_t broken[] = {'<', 'x', '>'};
	cot_test::owned_event owner;
	cot_diagnostic diagnostic{};

	EXPECT_EQ(cot_decode_ex(COT_FORMAT_XML, broken, sizeof(broken),
		&owner.value, &diagnostic), COT_STATUS_MALFORMED_MESSAGE);
	EXPECT_EQ(diagnostic.reason, COT_DIAGNOSTIC_MALFORMED_XML);
	EXPECT_LE(diagnostic.byte_offset, sizeof(broken));
	cot_buffer output{};

	EXPECT_EQ(cot_encode_ex(COT_FORMAT_XML, nullptr, &output, &diagnostic),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(diagnostic.reason, COT_DIAGNOSTIC_INVALID_ARGUMENT);
	EXPECT_EQ(diagnostic.byte_offset, COT_DIAGNOSTIC_NO_OFFSET);
}


TEST(Diagnostics, AllocationFailurePreservesEmptyOutputAndReportsReason)
{
	cot_test::allocation_failure failure(1);
	cot_test::owned_event owner;
	cot_diagnostic diagnostic{};
	const uint8_t wire[] = {0x12, 0};

	EXPECT_EQ(cot_decode_ex(COT_FORMAT_PROTOBUF, wire, sizeof(wire),
		&owner.value, &diagnostic), COT_STATUS_ALLOCATION_FAILURE);
	EXPECT_EQ(owner.value, nullptr);
	EXPECT_EQ(diagnostic.reason, COT_DIAGNOSTIC_ALLOCATION_FAILURE);
	EXPECT_EQ(diagnostic.byte_offset, COT_DIAGNOSTIC_NO_OFFSET);
}

TEST(Diagnostics, EncodingLocatesInvalidCoreAndResidualValues)
{
	cot_event event = cot_test::event();
	cot_buffer output{};
	cot_diagnostic diagnostic{};

	event.send_time_ms = UINT64_MAX;
	EXPECT_EQ(cot_encode_ex(COT_FORMAT_XML, &event, &output, &diagnostic),
		COT_STATUS_UNSUPPORTED_CONTENT);
	EXPECT_EQ(diagnostic.reason, COT_DIAGNOSTIC_INVALID_VALUE);
	EXPECT_STREQ(diagnostic.path, "send_time_ms");
	EXPECT_EQ(diagnostic.byte_offset, COT_DIAGNOSTIC_NO_OFFSET);
	event.send_time_ms = 0;
	cot_detail detail{};

	detail.xml = {"<broken>", 8};
	event.detail = &detail;
	EXPECT_EQ(cot_encode_ex(COT_FORMAT_XML, &event, &output, &diagnostic),
		COT_STATUS_UNSUPPORTED_CONTENT);
	EXPECT_EQ(diagnostic.reason, COT_DIAGNOSTIC_UNSUPPORTED_CONTENT);
	EXPECT_STREQ(diagnostic.path, "detail.xml");
	EXPECT_EQ(output.data, nullptr);
}

TEST(Diagnostics, UnknownGroupsAndWrongWireTypesCountAsDiscardedFields)
{
	std::vector<uint8_t> fields{0x0b, 0x08, 0x01, 0x0c, 0x28, 0x01};
	auto wire = cot_test::protobuf_event(fields);
	cot_test::owned_event owner;
	cot_diagnostic diagnostic{};

	ASSERT_EQ(cot_decode_ex(COT_FORMAT_PROTOBUF, wire.data(), wire.size(),
		&owner.value, &diagnostic), COT_STATUS_OK);
	EXPECT_EQ(diagnostic.unknown_protobuf_fields, 2u);
	EXPECT_EQ(diagnostic.unknown_protobuf_bytes, fields.size());
}


TEST(Inspection, MetadataLabelsAreStableDescriptiveStringsForCAndCpp)
{
	const char *const types[] = {
		"text", "uint64", "uint32", "double", "extensions"};
	const char *const units[] = {"none", "utc_milliseconds", "degrees",
		"metres", "metres_per_second", "percent"};
	const char *const groups[] = {"event", "point", "detail",
		"detail.contact", "detail.group", "detail.precision_location",
		"detail.status", "detail.takv", "detail.track"};

	for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
		const char *label = cot_field_type_string(
			static_cast<cot_field_type>(i));

		ASSERT_NE(label, nullptr);
		EXPECT_STREQ(label, types[i]);
		EXPECT_EQ(label, cot_test_c_field_type_string(i));
	}
	for (unsigned i = 0; i < sizeof(units) / sizeof(units[0]); ++i) {
		const char *label = cot_field_unit_string(
			static_cast<cot_field_unit>(i));

		ASSERT_NE(label, nullptr);
		EXPECT_STREQ(label, units[i]);
		EXPECT_EQ(label, cot_test_c_field_unit_string(i));
	}
	for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
		const char *label = cot_group_id_string(
			static_cast<cot_group_id>(i));

		ASSERT_NE(label, nullptr);
		EXPECT_STREQ(label, groups[i]);
		EXPECT_EQ(label, cot_test_c_group_id_string(i));
	}
}

TEST(Inspection, MetadataLabelsRejectUnknownEnumRepresentationsFromC)
{
	for (int invalid : {-1, 5, 999})
		EXPECT_EQ(cot_test_c_field_type_string(invalid), nullptr);
	for (int invalid : {-1, 6, 999})
		EXPECT_EQ(cot_test_c_field_unit_string(invalid), nullptr);
	for (int invalid : {-1, 9, 999})
		EXPECT_EQ(cot_test_c_group_id_string(invalid), nullptr);
}

}
