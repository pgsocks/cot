#include <gtest/gtest.h>
#include "support.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

using namespace cot_test;

extern "C" cot_status cot_test_c_convert(const uint8_t *input,
	size_t size, cot_buffer *output);
extern "C" void cot_test_c_cleanup(cot_buffer *buffer);

static std::string counted(cot_text value)
{
	return value.size ? std::string(value.data, value.size) : std::string{};
}

static std::string rich_xml()
{
	return xml("<contact callsign='Alpha' endpoint='a:1:tcp'/>"
		"<__group name='Blue' role='Leader'/>"
		"<precisionlocation geopointsrc='GPS' altsrc='DTED'/>"
		"<status battery='0'/>"
		"<takv device='phone' platform='ATAK' "
		"os='Android' version='5'/>"
		"<track speed='0' course='0'/>"
		"<custom>keep &amp; preserve</custom>");
}

TEST(Ownership, NullAndEmptyCleanupIsRepeatable)
{
	cot_buffer empty{};

	cot_decoded_event_destroy(nullptr);
	cot_buffer_destroy(nullptr);
	cot_buffer_destroy(&empty);
	cot_buffer_destroy(&empty);
	EXPECT_EQ(empty.data, nullptr);
	EXPECT_EQ(empty.size, 0u);
	EXPECT_EQ(cot_decoded_event_get(nullptr), nullptr);
}

TEST(Ownership, DecodedNestedViewsDoNotBorrowInput)
{
	owned_event owner;
	std::string input = rich_xml();

	ASSERT_EQ(decode(COT_FORMAT_XML, input, owner), COT_STATUS_OK);
	ASSERT_NE(owner.value, nullptr);
	const cot_event *view = owner.get();

	ASSERT_NE(view, nullptr);
	ASSERT_NE(view->detail, nullptr);
	ASSERT_NE(view->detail->contact, nullptr);
	std::fill(input.begin(), input.end(), '\0');
	input.clear();
	input.shrink_to_fit();
	EXPECT_EQ(counted(view->uid), "alpha-1");
	EXPECT_EQ(counted(view->detail->contact->callsign), "Alpha");
	EXPECT_NE(counted(view->detail->xml).find("custom"), std::string::npos);
	EXPECT_EQ(owner.get(), view);
	owned_buffer output;

	ASSERT_EQ(cot_encode(COT_FORMAT_PROTOBUF, view, &output.value),
		COT_STATUS_OK);
	EXPECT_EQ(owner.get(), view);
	EXPECT_EQ(counted(view->detail->contact->callsign), "Alpha");
}

TEST(Ownership, ProtobufExtensionStorageDoesNotBorrowInput)
{
	auto entry = wire_uint(1, 42);

	append(entry, wire_string(2, std::string("\0\xff\n", 3)));
	auto input = protobuf_event(wire_message(15, wire_message(8, entry)));
	owned_event owner;

	ASSERT_EQ(decode(COT_FORMAT_PROTOBUF, input, owner), COT_STATUS_OK);
	const cot_event *view = owner.get();

	ASSERT_NE(view, nullptr);
	ASSERT_NE(view->detail, nullptr);
	ASSERT_EQ(view->detail->extension_count, 1u);
	ASSERT_NE(view->detail->extensions, nullptr);
	std::fill(input.begin(), input.end(), 0);
	input.clear();
	input.shrink_to_fit();
	const cot_extension &extension = view->detail->extensions[0];

	EXPECT_EQ(extension.id, 42u);
	ASSERT_EQ(extension.data.size, 3u);
	ASSERT_NE(extension.data.data, nullptr);
	EXPECT_EQ(extension.data.data[0], 0);
	EXPECT_EQ(extension.data.data[1], 255);
	EXPECT_EQ(extension.data.data[2], 10);
}

TEST(Ownership, EncodedBytesOutliveTheDecodedOwner)
{
	owned_buffer output;

	{
		owned_event owner;

		ASSERT_EQ(decode(COT_FORMAT_XML, rich_xml(), owner),
			COT_STATUS_OK);
		ASSERT_NE(owner.get(), nullptr);
		ASSERT_EQ(cot_encode(COT_FORMAT_PROTOBUF, owner.get(),
			&output.value), COT_STATUS_OK);
	}
	owned_event reread;

	ASSERT_EQ(cot_decode(COT_FORMAT_PROTOBUF, output.value.data,
		output.value.size, &reread.value), COT_STATUS_OK);
	ASSERT_NE(reread.get(), nullptr);
	EXPECT_EQ(counted(reread.get()->uid), "alpha-1");
	ASSERT_NE(reread.get()->detail, nullptr);
	ASSERT_NE(reread.get()->detail->contact, nullptr);
	EXPECT_EQ(counted(reread.get()->detail->contact->callsign), "Alpha");
}

TEST(Ownership, EncodingRetainsNoCallerStorage)
{
	owned_buffer output;

	{
		std::string uid = "borrowed-uid";
		std::string callsign = "borrowed-callsign";
		std::string residual = "<custom value='retained'/>";
		std::array<uint8_t, 4> data{0, 10, 255, 13};
		cot_extension extension{42, {data.data(), data.size()}};
		cot_contact contact{};
		cot_detail detail{};
		cot_event value = event();

		contact.callsign = text(callsign);
		detail.contact = &contact;
		detail.xml = text(residual);
		detail.extensions = &extension;
		detail.extension_count = 1;
		value.uid = text(uid);
		value.detail = &detail;
		ASSERT_EQ(cot_encode(COT_FORMAT_PROTOBUF, &value,
			&output.value),
			COT_STATUS_OK);
		uid.assign(uid.size(), 'x');
		callsign.assign(callsign.size(), 'x');
		residual.assign(residual.size(), 'x');
		data.fill(99);
	}
	owned_event owner;

	ASSERT_EQ(cot_decode(COT_FORMAT_PROTOBUF, output.value.data,
		output.value.size, &owner.value), COT_STATUS_OK);
	const cot_event *view = owner.get();

	ASSERT_NE(view, nullptr);
	EXPECT_EQ(counted(view->uid), "borrowed-uid");
	ASSERT_NE(view->detail, nullptr);
	ASSERT_NE(view->detail->contact, nullptr);
	EXPECT_EQ(counted(view->detail->contact->callsign),
		"borrowed-callsign");
	EXPECT_EQ(counted(view->detail->xml), "<custom value='retained'/>");
	ASSERT_EQ(view->detail->extension_count, 1u);
	ASSERT_NE(view->detail->extensions, nullptr);
	const cot_bytes data = view->detail->extensions[0].data;

	ASSERT_EQ(data.size, 4u);
	ASSERT_NE(data.data, nullptr);
	EXPECT_EQ(std::vector<uint8_t>(data.data, data.data + data.size),
		(std::vector<uint8_t>{0, 10, 255, 13}));
}

TEST(Ownership, NonemptyEncodeOutputKeepsItsOriginalAllocation)
{
	const size_t baseline = live_allocations();
	cot_event value = event();
	owned_buffer output;

	ASSERT_EQ(cot_encode(COT_FORMAT_XML, &value, &output.value),
		COT_STATUS_OK);
	uint8_t *pointer = output.value.data;
	const size_t size = output.value.size;
	const std::string original = bytes(output.value);

	EXPECT_EQ(cot_encode(COT_FORMAT_PROTOBUF, &value, &output.value),
		COT_STATUS_INVALID_ARGUMENT);
	EXPECT_EQ(output.value.data, pointer);
	EXPECT_EQ(output.value.size, size);
	EXPECT_EQ(bytes(output.value), original);
	cot_buffer_destroy(&output.value);
	EXPECT_EQ(output.value.data, nullptr);
	EXPECT_EQ(output.value.size, 0u);
	EXPECT_EQ(live_allocations(), baseline);
	cot_buffer_destroy(&output.value);
	EXPECT_EQ(live_allocations(), baseline);
}

TEST(Ownership, NonnullDecodeOutputKeepsItsOriginalOwner)
{
	owned_event owner;
	const std::string first = xml("<contact callsign='Alpha'/>");

	ASSERT_EQ(decode(COT_FORMAT_XML, first, owner), COT_STATUS_OK);
	cot_decoded_event *original = owner.value;
	const cot_event *view = owner.get();

	ASSERT_NE(view, nullptr);
	for (cot_format format : {COT_FORMAT_XML, COT_FORMAT_PROTOBUF}) {
		const uint8_t input = 0;

		EXPECT_EQ(cot_decode(format, &input, 1, &owner.value),
			COT_STATUS_INVALID_ARGUMENT);
		EXPECT_EQ(owner.value, original);
		EXPECT_EQ(owner.get(), view);
		EXPECT_EQ(counted(view->uid), "alpha-1");
	}
}

TEST(Ownership, UnsupportedXmlEncodingDoesNotInvalidateOwner)
{
	const auto input = protobuf_event(
		wire_message(15, wire_message(8, {})));
	owned_event owner;

	ASSERT_EQ(decode(COT_FORMAT_PROTOBUF, input, owner), COT_STATUS_OK);
	const cot_event *view = owner.get();

	ASSERT_NE(view, nullptr);
	owned_buffer failure;

	EXPECT_EQ(cot_encode(COT_FORMAT_XML, view, &failure.value),
		COT_STATUS_UNSUPPORTED_CONTENT);
	EXPECT_EQ(failure.value.data, nullptr);
	EXPECT_EQ(failure.value.size, 0u);
	EXPECT_EQ(owner.get(), view);
	owned_buffer output;

	EXPECT_EQ(cot_encode(COT_FORMAT_PROTOBUF, view, &output.value),
		COT_STATUS_OK);
}

TEST(Ownership, FailedDecodeReleasesTemporaryStorageAndCanRetry)
{
	std::string truncated = rich_xml();

	truncated.erase(truncated.size() - std::string("</event>").size());
	auto fields = wire_string(5, "parsed-before-error");

	append(fields, wire_message(15,
		wire_message(2, wire_string(2, "Alpha"))));
	fields.push_back(0x80);
	const auto malformed = protobuf_event(fields);
	auto control = protobuf_event(wire_string(5, "alpha-1"));

	append(control, wire_message(1, {}));
	struct failed_input {
		cot_format format;
		std::string input;
		cot_status status;
	};
	const std::array<failed_input, 4> inputs{{
		{COT_FORMAT_XML, truncated, COT_STATUS_MALFORMED_MESSAGE},
		{COT_FORMAT_XML, rich_xml() + "trailing",
			COT_STATUS_MALFORMED_MESSAGE},
		{COT_FORMAT_PROTOBUF, {malformed.begin(), malformed.end()},
			COT_STATUS_MALFORMED_MESSAGE},
		{COT_FORMAT_PROTOBUF, {control.begin(), control.end()},
			COT_STATUS_UNSUPPORTED_CONTENT},
	}};
	const size_t baseline = live_allocations();

	for (size_t i = 0; i < inputs.size(); ++i) {
		SCOPED_TRACE(i);
		{
			owned_event owner;

			EXPECT_EQ(decode(inputs[i].format, inputs[i].input,
				owner), inputs[i].status);
			EXPECT_EQ(owner.value, nullptr);
		}
		EXPECT_EQ(live_allocations(), baseline);
		{
			owned_event retry;

			EXPECT_EQ(decode(COT_FORMAT_XML, rich_xml(), retry),
				COT_STATUS_OK);
		}
		EXPECT_EQ(live_allocations(), baseline);
	}
}

TEST(Ownership, UnsupportedXmlEncodingReleasesTemporaryStorage)
{
	cot_event value = event();
	cot_detail detail{};
	const std::string fragment = "<custom><nested>text</nested>";
	const std::string original = fragment;
	const size_t baseline = live_allocations();

	detail.xml = text(fragment);
	value.detail = &detail;
	{
		owned_buffer output;

		EXPECT_EQ(cot_encode(COT_FORMAT_XML, &value, &output.value),
			COT_STATUS_UNSUPPORTED_CONTENT);
		EXPECT_EQ(output.value.data, nullptr);
		EXPECT_EQ(output.value.size, 0u);
	}
	EXPECT_EQ(live_allocations(), baseline);
	EXPECT_EQ(fragment, original);
	{
		owned_buffer retry;

		EXPECT_EQ(cot_encode(COT_FORMAT_PROTOBUF, &value, &retry.value),
			COT_STATUS_OK);
	}
	EXPECT_EQ(live_allocations(), baseline);
}

TEST(CAbi, C11CallerConvertsAndCleansUpThroughTheSameSymbols)
{
	const std::string input = rich_xml();
	owned_buffer output;

	ASSERT_EQ(cot_test_c_convert(
		reinterpret_cast<const uint8_t *>(input.data()), input.size(),
		&output.value), COT_STATUS_OK);
	owned_event owner;

	ASSERT_EQ(cot_decode(COT_FORMAT_PROTOBUF, output.value.data,
		output.value.size, &owner.value), COT_STATUS_OK);
	ASSERT_NE(owner.get(), nullptr);
	EXPECT_EQ(counted(owner.get()->uid), "alpha-1");
	cot_test_c_cleanup(&output.value);
	EXPECT_EQ(output.value.data, nullptr);
	EXPECT_EQ(output.value.size, 0u);
}

class AllocationRollback : public testing::TestWithParam<cot_format> {};

TEST_P(AllocationRollback, EncodeFailsAtEveryAllocationAndCanRetry)
{
	owned_event source;

	ASSERT_EQ(decode(COT_FORMAT_XML, rich_xml(), source), COT_STATUS_OK);
	ASSERT_NE(source.get(), nullptr);
	ASSERT_NE(source.get()->detail, nullptr);
	cot_event value = *source.get();
	cot_detail detail = *value.detail;
	const uint8_t extension_data[] = {0, 255, 10};
	const cot_extension extensions[] = {
		{42, {extension_data, sizeof(extension_data)}},
		{0, {nullptr, 0}},
		{42, {extension_data, 1}},
	};

	if (GetParam() == COT_FORMAT_PROTOBUF) {
		detail.extensions = extensions;
		detail.extension_count = 3;
		value.detail = &detail;
	}
	size_t allocations;
	const size_t baseline = live_allocations();

	{
		allocation_failure measure(0);
		owned_buffer output;

		ASSERT_EQ(cot_encode(GetParam(), &value, &output.value),
			COT_STATUS_OK);
		allocations = measure.attempts();
	}
	ASSERT_GT(allocations, 0u);
	EXPECT_EQ(live_allocations(), baseline);
	for (size_t i = 1; i <= allocations; ++i) {
		SCOPED_TRACE(i);
		{
			allocation_failure fail(i);
			owned_buffer output;

			EXPECT_EQ(cot_encode(GetParam(), &value,
				&output.value), COT_STATUS_ALLOCATION_FAILURE);
			EXPECT_EQ(output.value.data, nullptr);
			EXPECT_EQ(output.value.size, 0u);
		}
		EXPECT_EQ(live_allocations(), baseline);
		{
			owned_buffer retry;

			ASSERT_EQ(cot_encode(GetParam(), &value,
				&retry.value), COT_STATUS_OK);
		}
		EXPECT_EQ(live_allocations(), baseline);
		EXPECT_EQ(counted(source.get()->uid), "alpha-1");
	}
}

TEST_P(AllocationRollback, DecodeFailsAtEveryAllocationAndCanRetry)
{
	owned_event source;
	owned_buffer encoded;

	ASSERT_EQ(decode(COT_FORMAT_XML, rich_xml(), source), COT_STATUS_OK);
	ASSERT_NE(source.get(), nullptr);
	ASSERT_NE(source.get()->detail, nullptr);
	cot_event value = *source.get();
	cot_detail detail = *value.detail;
	const uint8_t extension_data[] = {0, 255, 10};
	const cot_extension extensions[] = {
		{42, {extension_data, sizeof(extension_data)}},
		{0, {nullptr, 0}},
		{42, {extension_data, 1}},
	};

	if (GetParam() == COT_FORMAT_PROTOBUF) {
		detail.extensions = extensions;
		detail.extension_count = 3;
		value.detail = &detail;
	}
	ASSERT_EQ(cot_encode(GetParam(), &value, &encoded.value),
		COT_STATUS_OK);
	const std::string original = bytes(encoded.value);
	size_t allocations;
	const size_t baseline = live_allocations();

	{
		allocation_failure measure(0);
		owned_event owner;

		ASSERT_EQ(decode(GetParam(), original, owner), COT_STATUS_OK);
		allocations = measure.attempts();
	}
	ASSERT_GT(allocations, 0u);
	EXPECT_EQ(live_allocations(), baseline);
	for (size_t i = 1; i <= allocations; ++i) {
		SCOPED_TRACE(i);
		{
			allocation_failure fail(i);
			owned_event owner;

			EXPECT_EQ(decode(GetParam(), original, owner),
				COT_STATUS_ALLOCATION_FAILURE);
			EXPECT_EQ(owner.value, nullptr);
		}
		EXPECT_EQ(live_allocations(), baseline);
		{
			owned_event retry;

			ASSERT_EQ(decode(GetParam(), original, retry),
				COT_STATUS_OK);
		}
		EXPECT_EQ(live_allocations(), baseline);
		EXPECT_EQ(bytes(encoded.value), original);
	}
}

INSTANTIATE_TEST_SUITE_P(Formats, AllocationRollback,
	testing::Values(COT_FORMAT_XML, COT_FORMAT_PROTOBUF));

static bool roundtrip_matches(const cot_event *value, const std::string &uid)
{
	owned_buffer output;
	owned_event owner;

	if (cot_encode(COT_FORMAT_PROTOBUF, value, &output.value) !=
		COT_STATUS_OK)
		return false;
	if (cot_decode(COT_FORMAT_PROTOBUF, output.value.data,
		output.value.size, &owner.value) != COT_STATUS_OK)
		return false;
	return owner.get() && counted(owner.get()->uid) == uid;
}

TEST(Concurrency, SharedImmutableOwnerCanBeEncodedBySeveralThreads)
{
	owned_event owner;

	ASSERT_EQ(decode(COT_FORMAT_XML, rich_xml(), owner), COT_STATUS_OK);
	const cot_event *view = owner.get();

	ASSERT_NE(view, nullptr);
	std::atomic<unsigned> failures{0};
	std::array<std::thread, 4> workers;

	for (auto &worker : workers) {
		worker = std::thread([&] {
			for (unsigned i = 0; i < 4; ++i) {
				if (owner.get() != view ||
					!roundtrip_matches(view, "alpha-1"))
					++failures;
			}
		});
	}
	for (auto &worker : workers)
		worker.join();
	EXPECT_EQ(failures.load(), 0u);
	EXPECT_EQ(owner.get(), view);
	EXPECT_EQ(counted(view->uid), "alpha-1");
}

TEST(Concurrency, IndependentCallsDoNotMixTheirEventData)
{
	std::atomic<unsigned> failures{0};
	std::array<std::thread, 4> workers;
	const size_t baseline = live_allocations();

	for (size_t i = 0; i < workers.size(); ++i) {
		workers[i] = std::thread([&, i] {
			const std::string uid = "thread-" + std::to_string(i);
			cot_event value = event();

			value.uid = text(uid);
			if (!roundtrip_matches(&value, uid))
				++failures;
		});
	}
	for (auto &worker : workers)
		worker.join();
	EXPECT_EQ(failures.load(), 0u);
	EXPECT_EQ(live_allocations(), baseline);
}
