#include "context.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <thread>

extern "C" cot_status cot_test_c_encode_format(
	int, const cot_event *, cot_buffer *);
extern "C" const char *cot_test_c_status(int);

namespace
{

GIVEN("^(empty bytes|XML with .+|Protobuf with .+)$")
{
	REGEX_PARAM(std::string, name);
	ScenarioScope<EventContext> c;

	c->input = fixture(name);
}

WHEN("^I decode (?:the entire input|the input) as (XML|Protobuf) into a null "
	"owner$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<EventContext> c;

	ASSERT_EQ(c->owner.value, nullptr);
	c->decode(format(kind), c->input);
}

THEN("^decoding returns (COT_STATUS_[A-Z_]+)$")
{
	REGEX_PARAM(std::string, name);
	ScenarioScope<EventContext> c;
	cot_status expected = name == "COT_STATUS_OK" ? COT_STATUS_OK
			      : name == "COT_STATUS_MALFORMED_MESSAGE"
				      ? COT_STATUS_MALFORMED_MESSAGE
			      : name == "COT_STATUS_INVALID_ARGUMENT"
				      ? COT_STATUS_INVALID_ARGUMENT
				      : COT_STATUS_UNSUPPORTED_CONTENT;

	EXPECT_EQ(c->decoded, expected);
}

THEN("^the output owner remains null$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(c->owner.value, nullptr);
}

GIVEN("^an otherwise valid Protobuf TakMessage containing (.*)$")
{
	REGEX_PARAM(std::string, content);
	ScenarioScope<EventContext> c;

	c->input = fixture(content);
}

GIVEN("^a Protobuf TakMessage with a valid control followed by a truncated "
	"event$")
{
	ScenarioScope<EventContext> c;

	c->input = std::string("\x0a\x00\x12\x10\x01", 5);
}

GIVEN("^an otherwise valid XML event with (.*)$")
{
	REGEX_PARAM(std::string, content);
	ScenarioScope<EventContext> c;

	c->input = fixture(content);
}

GIVEN("^a valid Protobuf event containing (a NUL byte inside the uid "
	"string|empty required core strings|an infinite latitude|a NaN "
	"HAE|negative CE|a typed track with NaN speed|a typed track with "
	"infinite course|a residual fragment with unclosed tags)$")
{
	REGEX_PARAM(std::string, value);
	ScenarioScope<EventContext> c;
	double nan = std::numeric_limits<double>::quiet_NaN();
	double inf = std::numeric_limits<double>::infinity();

	if (value == "a NUL byte inside the uid string")
		c->caller.uid = c->keep(std::string("al\0pha", 6));
	else if (value == "empty required core strings")
		c->caller.type = c->caller.uid = c->caller.how = {};
	else if (value == "an infinite latitude")
		c->caller.point.latitude = inf;
	else if (value == "a NaN HAE")
		c->caller.point.hae = nan;
	else if (value == "negative CE")
		c->caller.point.ce = -1;
	else if (value == "a typed track with NaN speed") {
		set_typed(*c, "track");
		c->track.speed = nan;
	} else if (value == "a typed track with infinite course") {
		set_typed(*c, "track");
		c->track.course = inf;
	} else if (value == "a residual fragment with unclosed tags") {
		c->caller.detail = &c->detail;
		c->detail.xml = c->keep("<unclosed>");
	} else {
		FAIL() << value;
	}
	c->input_format = COT_FORMAT_PROTOBUF;
	c->input = encoded_event(COT_FORMAT_PROTOBUF, c->caller);
}

THEN("^the represented value is preserved$")
{
	ScenarioScope<EventContext> c;

	same_event(c->view(), &c->caller);
}

GIVEN("^(an unknown format selector|a null encode event pointer|a null output "
	"pointer|null decode bytes with a positive size|a caller text"
	" span with "
	"null data and positive size|a caller text span containing invalid "
	"UTF-8|extension count 1 with a null extension array|an extension byte "
	"span with null data and positive size)$")
{
	REGEX_PARAM(std::string, argument);
	ScenarioScope<EventContext> c;

	c->argument = argument;
}

WHEN("^I invoke the affected public codec operation$")
{
	ScenarioScope<EventContext> c;
	const std::string &arg = c->argument;

	if (arg == "an unknown format selector")
		c->encoded = cot_test_c_encode_format(
			99, &c->caller, &c->output.value);
	else if (arg == "a null encode event pointer")
		c->encoded =
			cot_encode(COT_FORMAT_XML, nullptr, &c->output.value);
	else if (arg == "a null output pointer") {
		c->encoded = cot_encode(COT_FORMAT_XML, &c->caller, nullptr);
		EXPECT_EQ(cot_decode(COT_FORMAT_XML, nullptr, 0, nullptr),
			COT_STATUS_INVALID_ARGUMENT);
	} else if (arg == "null decode bytes with a positive size")
		c->encoded =
			cot_decode(COT_FORMAT_XML, nullptr, 1, &c->owner.value);
	else {
		if (arg ==
			"a caller text span with null data and positive size")
			c->caller.uid = {nullptr, 1};
		else if (arg == "a caller text span containing invalid UTF-8")
			c->caller.uid = c->keep(std::string(1, '\xff'));
		else {
			c->caller.detail = &c->detail;
			c->detail.extension_count = 1;
			if (arg == "an extension byte span with null data and "
				   "positive size") {
				c->extensions.push_back({42, {nullptr, 1}});
				c->detail.extensions = c->extensions.data();
			}
		}
		c->encoded = cot_encode(
			COT_FORMAT_PROTOBUF, &c->caller, &c->output.value);
	}
}

THEN("^it returns COT_STATUS_INVALID_ARGUMENT$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(c->encoded, COT_STATUS_INVALID_ARGUMENT);
}

THEN("^every valid empty output remains empty$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(c->output.value.data, nullptr);
	EXPECT_EQ(c->output.value.size, 0U);
	EXPECT_EQ(c->owner.value, nullptr);
}

GIVEN("^(?:a library-owned encoded buffer containing a valid message|a "
	"successfully encoded buffer)$")
{
	ScenarioScope<EventContext> c;

	c->baseline = live_allocations();
	c->encode(COT_FORMAT_PROTOBUF, &c->caller);
	ASSERT_EQ(c->encoded, COT_STATUS_OK);
	c->saved_data = c->output.value.data;
	c->original_bytes = bytes(c->output.value);
}

WHEN("^I pass that nonempty buffer as the output of another encode$")
{
	ScenarioScope<EventContext> c;

	c->encoded = cot_encode(COT_FORMAT_XML, &c->caller, &c->output.value);
}

THEN("^the original pointer, size, and bytes are unchanged$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(c->output.value.data, c->saved_data);
	EXPECT_EQ(bytes(c->output.value), c->original_bytes);
	EXPECT_EQ(c->output.value.size, c->original_bytes.size());
}

THEN("^destroying that buffer releases its allocation once$")
{
	ScenarioScope<EventContext> c;

	ASSERT_GT(live_allocations(), c->baseline);
	cot_buffer_destroy(&c->output.value);
	EXPECT_EQ(live_allocations(), c->baseline);
	cot_buffer_destroy(&c->output.value);
	EXPECT_EQ(live_allocations(), c->baseline);
}

GIVEN("^(?:a successfully decoded owner|I successfully decoded a valid XML "
	"event)$")
{
	ScenarioScope<EventContext> c;

	c->input = xml();
	c->decode(COT_FORMAT_XML, c->input);
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
	c->saved_owner = c->owner.value;
	c->saved_view = c->view();
}

WHEN("^I pass its nonnull variable as the output of another decode$")
{
	ScenarioScope<EventContext> c;

	c->decoded = cot_decode(COT_FORMAT_XML,
		reinterpret_cast<const uint8_t *>(c->input.data()),
		c->input.size(), &c->owner.value);
}

THEN("^the original owner and its view remain valid$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(c->owner.value, c->saved_owner);
	EXPECT_EQ(c->view(), c->saved_view);
	same_core(c->view(), &c->caller);
}

WHEN("^I overwrite and release the original input storage$")
{
	ScenarioScope<EventContext> c;

	std::fill(c->input.begin(), c->input.end(), 'x');
	std::string().swap(c->input);
}

THEN("^repeated get calls return the same immutable view$")
{
	ScenarioScope<EventContext> c;

	for (int i = 0; i < 10; ++i) {
		EXPECT_EQ(c->view(), c->saved_view);
		same_core(c->view(), &c->caller);
	}
}

THEN("^encoding that view as Protobuf reproduces its represented fields$")
{
	ScenarioScope<EventContext> c;
	owned_event result;

	c->encode(COT_FORMAT_PROTOBUF, c->view());
	ASSERT_EQ(c->encoded, COT_STATUS_OK);
	ASSERT_EQ(decode(COT_FORMAT_PROTOBUF, bytes(c->output.value), result),
		COT_STATUS_OK);
	same_event(result.get(), c->view());
}

THEN("^the owner can be destroyed after encoding$")
{
	ScenarioScope<EventContext> c;

	cot_decoded_event_destroy(c->owner.value);
	c->owner.value = nullptr;
	owned_event result;

	ASSERT_EQ(decode(COT_FORMAT_PROTOBUF, bytes(c->output.value), result),
		COT_STATUS_OK);
	same_core(result.get(), &c->caller);
}

GIVEN("^a caller-owned valid event with separately allocated text and details$")
{
	ScenarioScope<EventContext> c;

	c->caller.type = c->keep("a-f-G-U-C");
	c->caller.uid = c->keep("alpha-1");
	c->caller.how = c->keep("m-g");
	set_typed(*c, "contact");
	c->contact.callsign = c->keep("Alpha");
	c->detail.xml = c->keep("<custom value='keep'/>");
	c->original_xml = encoded_event(COT_FORMAT_PROTOBUF, c->caller);
}

WHEN("^I encode it as Protobuf successfully$")
{
	ScenarioScope<EventContext> c;

	c->encode(COT_FORMAT_PROTOBUF, &c->caller);
	ASSERT_EQ(c->encoded, COT_STATUS_OK);
}

WHEN("^I overwrite and release all caller-owned event storage$")
{
	ScenarioScope<EventContext> c;

	for (auto &value : c->storage)
		std::fill(value.begin(), value.end(), 'x');
	c->storage.clear();
	c->caller = {};
	c->contact = {};
	c->detail = {};
}

THEN("^decoding the encoded buffer reproduces the original represented fields$")
{
	ScenarioScope<EventContext> c;
	owned_event expected;

	ASSERT_EQ(decode(COT_FORMAT_PROTOBUF, c->original_xml, expected),
		COT_STATUS_OK);
	c->decode(COT_FORMAT_PROTOBUF, bytes(c->output.value));
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
	same_event(c->view(), expected.get());
}

GIVEN("^a valid (encode|decode) request$")
{
	REGEX_PARAM(std::string, operation);
	ScenarioScope<EventContext> c;

	c->argument = operation;
	c->input = xml("<contact callsign='Alpha'/><custom value='keep'/>");
}

GIVEN("^a test allocator that fails each library allocation position in turn$")
{
	ScenarioScope<EventContext> c;

	c->baseline = live_allocations();
	c->failure_sweep = true;
}

WHEN("^I perform the (encode|decode) into a valid empty output$")
{
	REGEX_PARAM(std::string, operation);
	ScenarioScope<EventContext> c;

	ASSERT_TRUE(c->failure_sweep);
	ASSERT_EQ(operation, c->argument);
	size_t allocations;

	{
		allocation_failure count(0);
		owned_buffer buffer;
		owned_event owner;
		cot_status status =
			operation == "encode"
				? cot_encode(COT_FORMAT_XML, &c->caller,
					  &buffer.value)
				: decode(COT_FORMAT_XML, c->input, owner);

		ASSERT_EQ(status, COT_STATUS_OK);
		allocations = count.attempts();
	}
	ASSERT_GT(allocations, 0U);
	for (size_t i = 1; i <= allocations; ++i) {
		allocation_failure failure(i);
		owned_buffer buffer;
		owned_event owner;
		cot_status status =
			operation == "encode"
				? cot_encode(COT_FORMAT_XML, &c->caller,
					  &buffer.value)
				: decode(COT_FORMAT_XML, c->input, owner);

		SCOPED_TRACE(i);
		ASSERT_EQ(status, COT_STATUS_ALLOCATION_FAILURE);
		EXPECT_EQ(buffer.value.data, nullptr);
		EXPECT_EQ(buffer.value.size, 0U);
		EXPECT_EQ(owner.value, nullptr);
		EXPECT_EQ(live_allocations(), c->baseline);
		c->calls.push_back(status);
	}
}

THEN("^each injected failure returns COT_STATUS_ALLOCATION_FAILURE$")
{
	ScenarioScope<EventContext> c;

	ASSERT_FALSE(c->calls.empty());
	for (cot_status status : c->calls)
		EXPECT_EQ(status, COT_STATUS_ALLOCATION_FAILURE);
}

THEN("^the output remains empty on each failure$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(c->output.value.data, nullptr);
	EXPECT_EQ(c->output.value.size, 0U);
	EXPECT_EQ(c->owner.value, nullptr);
	ASSERT_FALSE(c->calls.empty());
}

THEN("^no temporary allocation is retained$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(live_allocations(), c->baseline);
}

THEN("^a later attempt without injected failure succeeds$")
{
	ScenarioScope<EventContext> c;

	if (c->argument == "encode") {
		c->encode(COT_FORMAT_XML, &c->caller);
		EXPECT_EQ(c->encoded, COT_STATUS_OK);
	} else {
		c->decode(COT_FORMAT_XML, c->input);
		EXPECT_EQ(c->decoded, COT_STATUS_OK);
	}
}

GIVEN("^an empty buffer and a null decoded owner$")
{
	ScenarioScope<EventContext> c;

	ASSERT_EQ(c->output.value.data, nullptr);
	ASSERT_EQ(c->output.value.size, 0U);
	ASSERT_EQ(c->owner.value, nullptr);
}

WHEN("^(?:I destroy the buffer twice and destroy the null owner|I destroy it "
	"twice)$")
{
	ScenarioScope<EventContext> c;

	cot_buffer_destroy(&c->output.value);
	cot_buffer_destroy(&c->output.value);
	cot_buffer_destroy(nullptr);
	cot_decoded_event_destroy(nullptr);
}

THEN("^(?:the buffer remains empty|its data pointer is null and its size is "
	"zero)$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(c->output.value.data, nullptr);
	EXPECT_EQ(c->output.value.size, 0U);
}

THEN("^getting the null owner returns null$")
{
	EXPECT_EQ(cot_decoded_event_get(nullptr), nullptr);
}

THEN("^its allocation was released once$")
{
	ScenarioScope<EventContext> c;

	EXPECT_EQ(live_allocations(), c->baseline);
}

WHEN("^I request text for every defined status and an unknown status value$")
{
	ScenarioScope<EventContext> c;

	for (int i : {0, 1, 2, 3, 4, 99}) {
		const char *value = cot_test_c_status(i);

		ASSERT_NE(value, nullptr);
		c->status_pointers.push_back(value);
		c->status_text.push_back(value);
	}
}

THEN("^every result is a nonnull immutable NUL-terminated string$")
{
	ScenarioScope<EventContext> c;

	ASSERT_EQ(c->status_text.size(), 6U);
	for (size_t i = 0; i < 6; ++i) {
		ASSERT_NE(c->status_pointers[i], nullptr);
		EXPECT_EQ(strlen(c->status_pointers[i]),
			c->status_text[i].size());
		EXPECT_STREQ(c->status_pointers[i], c->status_text[i].c_str());
	}
}

THEN("^the unknown status text is \"unknown status\"$")
{
	ScenarioScope<EventContext> c;

	ASSERT_EQ(c->status_text.size(), 6U);
	EXPECT_EQ(c->status_text.back(), "unknown status");
}

THEN("^later codec calls do not invalidate any returned string$")
{
	ScenarioScope<EventContext> c;

	c->roundtrip({COT_FORMAT_XML, COT_FORMAT_PROTOBUF});
	for (size_t i = 0; i < c->status_text.size(); ++i)
		EXPECT_STREQ(c->status_pointers[i], c->status_text[i].c_str());
}

GIVEN("^two independently decoded event owners$")
{
	ScenarioScope<EventContext> c;

	c->decode(COT_FORMAT_XML, xml("<contact callsign='Alpha'/>"));
	ASSERT_EQ(c->decoded, COT_STATUS_OK);
	ASSERT_EQ(decode(COT_FORMAT_XML, xml("<track speed='2' course='3'/>"),
			  c->other),
		COT_STATUS_OK);
	ASSERT_NE(c->view(), c->other.get());
}

static void concurrent(const cot_event *a, const cot_event *b)
{
	std::atomic<unsigned> failures{0};
	std::vector<std::thread> threads;
	std::string first = encoded_event(COT_FORMAT_PROTOBUF, *a);
	std::string second = encoded_event(COT_FORMAT_PROTOBUF, *b);

	for (unsigned i = 0; i < 4; ++i) {
		threads.emplace_back([&, i]() {
			const cot_event *view = i % 2 ? a : b;
			const std::string &expected = i % 2 ? first : second;

			for (unsigned n = 0; n < 30; ++n) {
				owned_buffer output;

				if (cot_encode(COT_FORMAT_PROTOBUF, view,
					    &output.value) != COT_STATUS_OK ||
					bytes(output.value) != expected)
					++failures;
			}
		});
	}
	for (auto &thread : threads)
		thread.join();
	EXPECT_EQ(failures.load(), 0U);
	EXPECT_EQ(encoded_event(COT_FORMAT_PROTOBUF, *a), first);
	EXPECT_EQ(encoded_event(COT_FORMAT_PROTOBUF, *b), second);
}

WHEN("^separate threads read and encode their views concurrently$")
{
	ScenarioScope<EventContext> c;

	concurrent(c->view(), c->other.get());
}

WHEN("^multiple threads read and encode one shared view before destruction$")
{
	ScenarioScope<EventContext> c;

	concurrent(c->view(), c->view());
}

THEN("^all calls return their expected results without changing either view$")
{
	ScenarioScope<EventContext> c;
	owned_event first;
	owned_event second;

	ASSERT_EQ(decode(COT_FORMAT_XML, xml("<contact callsign='Alpha'/>"),
			  first),
		COT_STATUS_OK);
	ASSERT_EQ(decode(COT_FORMAT_XML, xml("<track speed='2' course='3'/>"),
			  second),
		COT_STATUS_OK);
	same_event(c->view(), first.get());
	same_event(c->other.get(), second.get());
}

THEN("^both owners can be destroyed after the threads finish$")
{
	ScenarioScope<EventContext> c;
	size_t before = live_allocations();

	cot_decoded_event_destroy(c->owner.value);
	cot_decoded_event_destroy(c->other.value);
	c->owner.value = c->other.value = nullptr;
	EXPECT_LT(live_allocations(), before);
}

} // namespace
