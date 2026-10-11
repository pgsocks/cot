#include <gtest/gtest.h>
#include "support.h"

#include <array>
#include <cstring>

using namespace cot_test;

extern "C" cot_status cot_test_c_encode_format(int format,
	const cot_event *event, cot_buffer *output);
extern "C" cot_status cot_test_c_decode_format(int format,
	const uint8_t *data, size_t size, cot_decoded_event **output);
extern "C" const char *cot_test_c_status(int status);

class EncodeArguments : public testing::TestWithParam<cot_format> {};

TEST_P(EncodeArguments, RejectsNullEventAndPreservesEmptyOutput)
{
	owned_buffer output;

	EXPECT_EQ(cot_encode(GetParam(), nullptr, &output.value),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(output.value.data, nullptr);
	EXPECT_EQ(output.value.size, 0u);
}

TEST_P(EncodeArguments, RejectsNullOutput)
{
	cot_event value = event();

	EXPECT_EQ(cot_encode(GetParam(), &value, nullptr),
		COT_STATUS_INVALID_ARGUMENT);
}

TEST_P(EncodeArguments, RejectsNonemptyOutputWithoutMutation)
{
	cot_event value = event();
	uint8_t marker = 42;
	cot_buffer output{&marker, 1};

	EXPECT_EQ(cot_encode(GetParam(), &value, &output),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(output.data, &marker);
	EXPECT_EQ(output.size, 1u);
	EXPECT_EQ(marker, 42);
}

TEST_P(EncodeArguments, RejectsNonemptyOutputPointerWithoutMutation)
{
	cot_event value = event();
	uint8_t marker = 42;
	cot_buffer output{&marker, 0};

	EXPECT_EQ(cot_encode(GetParam(), &value, &output),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(output.data, &marker);
	EXPECT_EQ(output.size, 0u);
	EXPECT_EQ(marker, 42);
}

TEST_P(EncodeArguments, RejectsInconsistentOutputSizeWithoutMutation)
{
	cot_event value = event();
	cot_buffer output{nullptr, 1};

	EXPECT_EQ(cot_encode(GetParam(), &value, &output),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(output.data, nullptr);
	EXPECT_EQ(output.size, 1u);
}

TEST_P(EncodeArguments, RejectsNullCoreTextWithPositiveCount)
{
	const std::array<cot_text cot_event::*, 8> fields{
		&cot_event::type, &cot_event::access, &cot_event::qos,
		&cot_event::opex, &cot_event::uid, &cot_event::how,
		&cot_event::caveat, &cot_event::releasable_to
	};

	for (size_t i = 0; i < fields.size(); ++i) {
		SCOPED_TRACE(i);
		cot_event value = event();
		owned_buffer output;

		value.*fields[i] = {nullptr, 1};
		EXPECT_EQ(cot_encode(GetParam(), &value, &output.value),
			COT_STATUS_INVALID_ARGUMENT);
		EXPECT_EQ(output.value.data, nullptr);
		EXPECT_EQ(output.value.size, 0u);
	}
}

TEST_P(EncodeArguments, RejectsInvalidUtf8InEveryCoreText)
{
	const std::array<cot_text cot_event::*, 8> fields{
		&cot_event::type, &cot_event::access, &cot_event::qos,
		&cot_event::opex, &cot_event::uid, &cot_event::how,
		&cot_event::caveat, &cot_event::releasable_to
	};
	const std::array<std::string, 5> invalid{
		"\xc0\x80", "\x80", "\xe2\x82", "\xed\xa0\x80",
		"\xf4\x90\x80\x80"
	};

	for (size_t i = 0; i < fields.size(); ++i) {
		for (size_t j = 0; j < invalid.size(); ++j) {
			SCOPED_TRACE(testing::Message() << i << ":" << j);
			cot_event value = event();
			owned_buffer output;

			value.*fields[i] = text(invalid[j]);
			EXPECT_EQ(cot_encode(GetParam(), &value, &output.value),
				COT_STATUS_INVALID_ARGUMENT);
			EXPECT_EQ(output.value.data, nullptr);
			EXPECT_EQ(output.value.size, 0u);
		}
	}
}

TEST_P(EncodeArguments, ValidatesEveryNestedTextSpan)
{
	cot_contact contact{};
	cot_group group{};
	cot_precision_location precision{};
	cot_takv takv{};
	cot_detail detail{};
	cot_event value = event();

	detail.contact = &contact;
	detail.group = &group;
	detail.precision_location = &precision;
	detail.takv = &takv;
	value.detail = &detail;
	const std::array<cot_text *, 12> fields{
		&contact.endpoint, &contact.callsign, &contact.altendpoints,
		&group.name, &group.role, &precision.geopointsrc,
		&precision.altsrc, &takv.device, &takv.platform, &takv.os,
		&takv.version, &detail.xml
	};
	const std::string invalid_utf8 = "\xff";

	for (size_t i = 0; i < fields.size(); ++i) {
		SCOPED_TRACE(i);
		for (cot_text invalid : {cot_text{nullptr, 1},
			text(invalid_utf8)}) {
			owned_buffer output;

			*fields[i] = invalid;
			EXPECT_EQ(cot_encode(GetParam(), &value, &output.value),
				COT_STATUS_INVALID_ARGUMENT);
			EXPECT_EQ(output.value.data, nullptr);
			EXPECT_EQ(output.value.size, 0u);
		}
		*fields[i] = {};
	}
}

TEST_P(EncodeArguments, RejectsNullExtensionArrayWithPositiveCount)
{
	cot_detail detail{};
	cot_event value = event();
	owned_buffer output;

	detail.extension_count = 1;
	value.detail = &detail;
	EXPECT_EQ(cot_encode(GetParam(), &value, &output.value),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(output.value.data, nullptr);
	EXPECT_EQ(output.value.size, 0u);
}

TEST_P(EncodeArguments, RejectsNullExtensionBytesWithPositiveCount)
{
	cot_extension extension{42, {nullptr, 1}};
	cot_detail detail{};
	cot_event value = event();
	owned_buffer output;

	detail.extensions = &extension;
	detail.extension_count = 1;
	value.detail = &detail;
	EXPECT_EQ(cot_encode(GetParam(), &value, &output.value),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(output.value.data, nullptr);
	EXPECT_EQ(output.value.size, 0u);
}

INSTANTIATE_TEST_SUITE_P(Formats, EncodeArguments,
	testing::Values(COT_FORMAT_XML, COT_FORMAT_PROTOBUF));

class DecodeArguments : public testing::TestWithParam<cot_format> {};

TEST_P(DecodeArguments, RejectsNullBytesWithPositiveCount)
{
	owned_event owner;

	EXPECT_EQ(cot_decode(GetParam(), nullptr, 1, &owner.value),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(owner.value, nullptr);
}

TEST_P(DecodeArguments, RejectsNullOutput)
{
	const uint8_t input = 0;

	EXPECT_EQ(cot_decode(GetParam(), &input, 1, nullptr),
		COT_STATUS_INVALID_ARGUMENT);
}

TEST_P(DecodeArguments, EmptyInputIsNotAnInvalidPointerArgument)
{
	const uint8_t empty = 0;
	cot_status expected = GetParam() == COT_FORMAT_XML ?
		COT_STATUS_MALFORMED_MESSAGE : COT_STATUS_UNSUPPORTED_CONTENT;

	const std::array<const uint8_t *, 2> inputs{nullptr, &empty};

	for (const uint8_t *input : inputs) {
		owned_event owner;

		EXPECT_EQ(cot_decode(GetParam(), input, 0, &owner.value),
			expected);
		EXPECT_EQ(owner.value, nullptr);
	}
}

INSTANTIATE_TEST_SUITE_P(Formats, DecodeArguments,
	testing::Values(COT_FORMAT_XML, COT_FORMAT_PROTOBUF));

TEST(Arguments, UnknownFormatsFromCAbiAreRejected)
{
	cot_event value = event();
	const std::string input = xml();

	for (int format : {-1, 2, 127}) {
		SCOPED_TRACE(format);
		owned_event owner;
		owned_buffer output;

		EXPECT_EQ(cot_test_c_encode_format(format, &value,
			&output.value),
			COT_STATUS_INVALID_ARGUMENT);
		EXPECT_EQ(cot_test_c_decode_format(format,
			reinterpret_cast<const uint8_t *>(input.data()),
			input.size(), &owner.value),
			COT_STATUS_INVALID_ARGUMENT);
		EXPECT_EQ(output.value.data, nullptr);
		EXPECT_EQ(output.value.size, 0u);
		EXPECT_EQ(owner.value, nullptr);
	}
}

TEST(StatusText, DefinedStatusesHaveStaticNonemptyText)
{
	std::array<const char *, 5> saved{};
	std::array<std::string, 5> copies;

	for (int i = 0; i < 5; ++i) {
		saved[i] = cot_status_string(static_cast<cot_status>(i));
		ASSERT_NE(saved[i], nullptr);
		EXPECT_GT(std::strlen(saved[i]), 0u);
		copies[i] = saved[i];
	}
	owned_event owner;

	decode(COT_FORMAT_XML, "bad input", owner);
	for (int i = 0; i < 5; ++i)
		EXPECT_STREQ(saved[i], copies[i].c_str());
}

TEST(StatusText, UnknownStatusHasSpecifiedFallback)
{
	for (int value : {-1, 5, 127})
		EXPECT_STREQ(cot_test_c_status(value), "unknown status");
}
