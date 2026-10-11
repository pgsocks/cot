#include "support.h"

#include <cstring>
#include <limits>

namespace cot_test {

owned_buffer::~owned_buffer()
{
	cot_buffer_destroy(&value);
}

owned_event::~owned_event()
{
	cot_decoded_event_destroy(value);
}

const cot_event *owned_event::get() const
{
	return cot_decoded_event_get(value);
}

cot_text text(const std::string &value)
{
	return {value.data(), value.size()};
}

cot_event event()
{
	cot_event value{};

	value.type = {"a-f-G-U-C", 9};
	value.uid = {"alpha-1", 7};
	value.how = {"m-g", 3};
	value.send_time_ms = 1704067200123ULL;
	value.start_time_ms = 1704067200123ULL;
	value.stale_time_ms = 1704067260123ULL;
	value.point = {38, -77, 10, 2, 3};
	return value;
}

std::string xml(const std::string &detail)
{
	return "<event version='2.0' type='a-f-G-U-C' uid='alpha-1' "
		"how='m-g' time='2024-01-01T00:00:00.123Z' "
		"start='2024-01-01T00:00:00.123Z' "
		"stale='2024-01-01T00:01:00.123Z'>"
		"<point lat='38' lon='-77' hae='10' ce='2' le='3'/>"
		"<detail>" + detail + "</detail></event>";
}

std::string bytes(const cot_buffer &buffer)
{
	if (!buffer.size)
		return {};
	return {reinterpret_cast<const char *>(buffer.data), buffer.size};
}

cot_status decode(cot_format format, const std::string &input,
	owned_event &owner)
{
	return cot_decode(format,
		reinterpret_cast<const uint8_t *>(input.data()), input.size(),
		&owner.value);
}

cot_status decode(cot_format format, const std::vector<uint8_t> &input,
	owned_event &owner)
{
	return cot_decode(format, input.data(), input.size(), &owner.value);
}

static void varint(std::vector<uint8_t> &output, uint64_t value)
{
	while (value >= 128) {
		output.push_back(static_cast<uint8_t>((value & 127) | 128));
		value >>= 7;
	}
	output.push_back(static_cast<uint8_t>(value));
}

void append(std::vector<uint8_t> &output,
	const std::vector<uint8_t> &fields)
{
	output.insert(output.end(), fields.begin(), fields.end());
}

std::vector<uint8_t> wire_message(unsigned field,
	const std::vector<uint8_t> &value)
{
	std::vector<uint8_t> output;

	varint(output, (uint64_t{field} << 3) | 2);
	varint(output, value.size());
	append(output, value);
	return output;
}

std::vector<uint8_t> wire_string(unsigned field, const std::string &value)
{
	return wire_message(field, {value.begin(), value.end()});
}

std::vector<uint8_t> wire_uint(unsigned field, uint64_t value)
{
	std::vector<uint8_t> output;

	varint(output, uint64_t{field} << 3);
	varint(output, value);
	return output;
}

std::vector<uint8_t> wire_double(unsigned field, double value)
{
	static_assert(sizeof(double) == sizeof(uint64_t));
	static_assert(std::numeric_limits<double>::is_iec559);
	uint64_t bits;

	std::memcpy(&bits, &value, sizeof(bits));
	std::vector<uint8_t> output;

	varint(output, (uint64_t{field} << 3) | 1);
	for (unsigned i = 0; i < 8; ++i)
		output.push_back(static_cast<uint8_t>(bits >> (8 * i)));
	return output;
}

std::vector<uint8_t> protobuf_event(const std::vector<uint8_t> &fields)
{
	return wire_message(2, fields);
}

}
