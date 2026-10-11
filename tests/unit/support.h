#ifndef COT_TEST_SUPPORT_H
#define COT_TEST_SUPPORT_H

#include <cot/cot.hpp>
#include <string>
#include <vector>

namespace cot_test {

class owned_buffer {
public:
	cot_buffer value{};
	owned_buffer() = default;
	~owned_buffer();
	owned_buffer(const owned_buffer &) = delete;
	owned_buffer &operator=(const owned_buffer &) = delete;
};

class owned_event {
public:
	cot_decoded_event *value = nullptr;

	owned_event() = default;
	~owned_event();
	owned_event(const owned_event &) = delete;
	owned_event &operator=(const owned_event &) = delete;
	const cot_event *get() const;
};

/* text() borrows its argument; keep the string alive through the API call. */
cot_text text(const std::string &value);
cot_event event();
std::string xml(const std::string &detail = "");
std::string bytes(const cot_buffer &buffer);
cot_status decode(cot_format format, const std::string &input,
	owned_event &owner);
cot_status decode(cot_format format, const std::vector<uint8_t> &input,
	owned_event &owner);

/* Independent wire fixture constructors, using the pinned schema's tags. */
std::vector<uint8_t> wire_string(unsigned field, const std::string &value);
std::vector<uint8_t> wire_message(unsigned field,
	const std::vector<uint8_t> &value);
std::vector<uint8_t> wire_uint(unsigned field, uint64_t value);
std::vector<uint8_t> wire_double(unsigned field, double value);
std::vector<uint8_t> protobuf_event(const std::vector<uint8_t> &fields);
void append(std::vector<uint8_t> &output,
	const std::vector<uint8_t> &fields);

/* Fail one allocation on this thread; zero disables failure injection. */
class allocation_failure {
public:
	explicit allocation_failure(size_t index);
	~allocation_failure();
	allocation_failure(const allocation_failure &) = delete;
	allocation_failure &operator=(const allocation_failure &) = delete;
	size_t attempts() const;
};

size_t live_allocations();

}

#endif
