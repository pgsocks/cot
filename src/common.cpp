#include "internal.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <new>
#include <system_error>

struct alignas(max_align_t) cot_block {
	cot_block *next;
};

void *cot_arena_allocate(cot_arena *arena, size_t size)
{
	if (arena->status != COT_STATUS_OK)
		return nullptr;
	if (size > SIZE_MAX - sizeof(cot_block)) {
		arena->status = COT_STATUS_ALLOCATION_FAILURE;
		return nullptr;
	}
	void *storage = cot_allocate(sizeof(cot_block) + size);

	if (!storage) {
		arena->status = COT_STATUS_ALLOCATION_FAILURE;
		return nullptr;
	}
	cot_block *block = new (storage) cot_block{arena->head};

	arena->head = block;
	return block + 1;
}

void cot_arena_destroy(cot_arena *arena)
{
	while (arena->head) {
		cot_block *next = arena->head->next;

		cot_release(arena->head);
		arena->head = next;
	}
}

cot_text cot_copy_text(cot_arena *arena, const char *data, size_t size)
{
	if (!size)
		return {};
	char *copy = static_cast<char *>(cot_arena_allocate(arena, size));

	if (!copy)
		return {};
	memcpy(copy, data, size);
	return {copy, size};
}

bool cot_text_equal(cot_text text, const char *literal)
{
	size_t size = strlen(literal);

	return text.size == size &&
	       (!size || !memcmp(text.data, literal, size));
}

bool cot_utf8(cot_text text, bool xml)
{
	if (!text.data && text.size)
		return false;
	for (size_t i = 0; i < text.size;) {
		uint32_t value = static_cast<uint8_t>(text.data[i++]);
		unsigned remaining = 0;
		uint32_t minimum = 0;

		if (value >= 0xc2 && value <= 0xdf) {
			remaining = 1;
			minimum = 0x80;
			value &= 0x1f;
		} else if (value >= 0xe0 && value <= 0xef) {
			remaining = 2;
			minimum = 0x800;
			value &= 0x0f;
		} else if (value >= 0xf0 && value <= 0xf4) {
			remaining = 3;
			minimum = 0x10000;
			value &= 7;
		} else if (value >= 0x80) {
			return false;
		}
		if (remaining > text.size - i)
			return false;
		while (remaining--) {
			uint8_t next = static_cast<uint8_t>(text.data[i++]);

			if ((next & 0xc0) != 0x80)
				return false;
			value = (value << 6) | (next & 0x3f);
		}
		if (value < minimum || value > 0x10ffff ||
			(value >= 0xd800 && value <= 0xdfff))
			return false;
		if (xml && !(value == 9 || value == 10 || value == 13 ||
				   (value >= 0x20 && value <= 0xd7ff) ||
				   (value >= 0xe000 && value <= 0xfffd) ||
				   value >= 0x10000))
			return false;
	}
	return true;
}

void cot_write(cot_writer *writer, const void *data, size_t size)
{
	if (writer->status != COT_STATUS_OK || !size)
		return;
	if (size > SIZE_MAX - writer->size) {
		writer->status = COT_STATUS_ALLOCATION_FAILURE;
		return;
	}
	size_t needed = writer->size + size;

	if (!writer->counting && needed > writer->capacity) {
		size_t capacity = writer->capacity;

		if (capacity <= SIZE_MAX / 2)
			capacity *= 2;
		if (capacity < needed)
			capacity = needed;
		if (capacity < 128)
			capacity = 128;
		uint8_t *copy = static_cast<uint8_t *>(cot_allocate(capacity));

		if (!copy) {
			writer->status = COT_STATUS_ALLOCATION_FAILURE;
			return;
		}
		if (writer->size)
			memcpy(copy, writer->data, writer->size);
		cot_release(writer->data);
		writer->data = copy;
		writer->capacity = capacity;
	}
	if (!writer->counting)
		memcpy(writer->data + writer->size, data, size);
	writer->size = needed;
}

void cot_write_literal(cot_writer *writer, const char *literal)
{
	cot_write(writer, literal, strlen(literal));
}

void cot_writer_destroy(cot_writer *writer)
{
	cot_release(writer->data);
	writer->data = nullptr;
	writer->size = writer->capacity = 0;
}

cot_status cot_validate_event(const cot_event *event)
{
	const cot_text core[] = {event->type, event->access, event->qos,
		event->opex, event->uid, event->how, event->caveat,
		event->releasable_to};

	for (cot_text text : core)
		if (!cot_utf8(text, false))
			return COT_STATUS_INVALID_ARGUMENT;
	const cot_detail *detail = event->detail;

	if (!detail)
		return COT_STATUS_OK;
	if (!cot_utf8(detail->xml, false) ||
		(!detail->extensions && detail->extension_count) ||
		detail->extension_count > SIZE_MAX / sizeof(cot_extension))
		return COT_STATUS_INVALID_ARGUMENT;
	for (size_t i = 0; i < detail->extension_count; ++i)
		if (!detail->extensions[i].data.data &&
			detail->extensions[i].data.size)
			return COT_STATUS_INVALID_ARGUMENT;
	cot_text texts[11]{};
	size_t count = 0;

	if (detail->contact) {
		texts[count++] = detail->contact->endpoint;
		texts[count++] = detail->contact->callsign;
		texts[count++] = detail->contact->altendpoints;
	}
	if (detail->group) {
		texts[count++] = detail->group->name;
		texts[count++] = detail->group->role;
	}
	if (detail->precision_location) {
		texts[count++] = detail->precision_location->geopointsrc;
		texts[count++] = detail->precision_location->altsrc;
	}
	if (detail->takv) {
		texts[count++] = detail->takv->device;
		texts[count++] = detail->takv->platform;
		texts[count++] = detail->takv->os;
		texts[count++] = detail->takv->version;
	}
	for (size_t i = 0; i < count; ++i)
		if (!cot_utf8(texts[i], false))
			return COT_STATUS_INVALID_ARGUMENT;
	return COT_STATUS_OK;
}

bool cot_parse_number(cot_text text, double *number)
{
	if (!text.size)
		return false;
	const char *begin = text.data;
	const char *end = begin + text.size;

	if (*begin == '+')
		++begin;
	if (begin == end)
		return false;
	auto result = std::from_chars(
		begin, end, *number, std::chars_format::general);

	if (result.ptr != end)
		return false;
	if (result.ec == std::errc::result_out_of_range) {
		*number = *begin == '-' ? -HUGE_VAL : HUGE_VAL;
		return true;
	}
	return result.ec == std::errc{};
}

bool cot_parse_uint(cot_text text, uint64_t maximum, uint64_t *number)
{
	size_t i = 0;
	uint64_t value = 0;

	if (text.size && text.data[0] == '+')
		++i;
	if (i == text.size)
		return false;
	for (; i < text.size; ++i) {
		unsigned digit = static_cast<unsigned char>(text.data[i]) - '0';

		if (digit > 9 || digit > maximum ||
			value > (maximum - digit) / 10)
			return false;
		value = value * 10 + digit;
	}
	*number = value;
	return true;
}

bool cot_xml_point(const cot_point *point)
{
	return std::isfinite(point->latitude) && point->latitude >= -90 &&
	       point->latitude <= 90 && std::isfinite(point->longitude) &&
	       point->longitude >= -180 && point->longitude <= 180 &&
	       std::isfinite(point->hae) && std::isfinite(point->ce) &&
	       point->ce >= 0 && std::isfinite(point->le) && point->le >= 0;
}

static bool digits(const char *data, size_t count, unsigned *out)
{
	unsigned value = 0;

	for (size_t i = 0; i < count; ++i) {
		unsigned digit = static_cast<unsigned char>(data[i]) - '0';

		if (digit > 9)
			return false;
		value = value * 10 + digit;
	}
	*out = value;
	return true;
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
	year -= month <= 2;
	int era = (year >= 0 ? year : year - 399) / 400;
	unsigned y = static_cast<unsigned>(year - era * 400);
	unsigned m = month > 2 ? month - 3 : month + 9;
	unsigned days = (153 * m + 2) / 5 + day - 1;

	return int64_t{era} * 146097 + y * 365 + y / 4 - y / 100 + days -
	       719468;
}

cot_status cot_parse_timestamp(cot_text text, uint64_t *time)
{
	if (text.size < 20)
		return COT_STATUS_MALFORMED_MESSAGE;
	const char *s = text.data;
	unsigned y, m, d, h, min, sec;

	if (!digits(s, 4, &y) || s[4] != '-' || !digits(s + 5, 2, &m) ||
		s[7] != '-' || !digits(s + 8, 2, &d) ||
		(s[10] != 'T' && s[10] != 't') || !digits(s + 11, 2, &h) ||
		s[13] != ':' || !digits(s + 14, 2, &min) || s[16] != ':' ||
		!digits(s + 17, 2, &sec) || m < 1 || m > 12 || d < 1 ||
		h > 23 || min > 59 || sec > 60)
		return COT_STATUS_MALFORMED_MESSAGE;
	const unsigned month_days[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);

	if (d > month_days[m - 1] + (m == 2 && leap ? 1 : 0))
		return COT_STATUS_MALFORMED_MESSAGE;
	size_t i = 19;
	unsigned ms = 0;
	bool precision = false;

	if (s[i] == '.') {
		size_t start = ++i;

		while (i < text.size && s[i] >= '0' && s[i] <= '9') {
			if (i - start < 3)
				ms = ms * 10 + s[i] - '0';
			else if (s[i] != '0')
				precision = true;
			++i;
		}
		if (i == start)
			return COT_STATUS_MALFORMED_MESSAGE;
		for (size_t n = i - start; n < 3; ++n)
			ms *= 10;
	}
	if (i == text.size)
		return COT_STATUS_MALFORMED_MESSAGE;
	int offset = 0;

	if (s[i] == 'Z' || s[i] == 'z') {
		++i;
	} else if (s[i] == '+' || s[i] == '-') {
		unsigned oh, om;
		int sign = s[i] == '+' ? 1 : -1;

		if (text.size - i != 6 || !digits(s + i + 1, 2, &oh) ||
			s[i + 3] != ':' || !digits(s + i + 4, 2, &om) ||
			oh > 23 || om > 59)
			return COT_STATUS_MALFORMED_MESSAGE;
		offset = sign * static_cast<int>(oh * 60 + om);
		i += 6;
	} else {
		return COT_STATUS_MALFORMED_MESSAGE;
	}
	if (i != text.size)
		return COT_STATUS_MALFORMED_MESSAGE;
	int64_t value = (days_from_civil(y, m, d) * 86400 + h * 3600 +
				min * 60 + sec - int64_t{offset} * 60) *
				1000 +
			ms;

	if (precision || sec == 60 || value < 0 || value > 253402300799999LL)
		return COT_STATUS_UNSUPPORTED_CONTENT;
	*time = static_cast<uint64_t>(value);
	return COT_STATUS_OK;
}

void cot_write_uint(cot_writer *writer, uint64_t number)
{
	char digits_buffer[20];
	size_t size = 0;

	do {
		digits_buffer[size++] = static_cast<char>('0' + number % 10);
		number /= 10;
	} while (number);
	for (size_t i = 0; i < size / 2; ++i) {
		char digit = digits_buffer[i];

		digits_buffer[i] = digits_buffer[size - 1 - i];
		digits_buffer[size - 1 - i] = digit;
	}
	cot_write(writer, digits_buffer, size);
}

void cot_write_number(cot_writer *writer, double number)
{
	char buffer[32];
	auto result = std::to_chars(buffer, buffer + sizeof(buffer), number,
		std::chars_format::general, 17);

	if (result.ec != std::errc{}) {
		writer->status = COT_STATUS_UNSUPPORTED_CONTENT;
		return;
	}
	cot_write(writer, buffer, static_cast<size_t>(result.ptr - buffer));
}

static void fixed_digits(char *out, unsigned value, unsigned count)
{
	while (count) {
		out[--count] = static_cast<char>('0' + value % 10);
		value /= 10;
	}
}

void cot_write_timestamp(cot_writer *writer, uint64_t time)
{
	uint64_t seconds = time / 1000;
	unsigned z = static_cast<unsigned>(seconds / 86400) + 719468;
	unsigned era = z / 146097;
	unsigned doe = z - era * 146097;
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	unsigned year = yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	unsigned day = doy - (153 * mp + 2) / 5 + 1;
	unsigned month = mp < 10 ? mp + 3 : mp - 9;

	year += month <= 2;
	char out[] = "0000-00-00T00:00:00.000Z";

	fixed_digits(out, year, 4);
	fixed_digits(out + 5, month, 2);
	fixed_digits(out + 8, day, 2);
	fixed_digits(out + 11, static_cast<unsigned>(seconds / 3600 % 24), 2);
	fixed_digits(out + 14, static_cast<unsigned>(seconds / 60 % 60), 2);
	fixed_digits(out + 17, static_cast<unsigned>(seconds % 60), 2);
	fixed_digits(out + 20, static_cast<unsigned>(time % 1000), 3);
	cot_write(writer, out, sizeof(out) - 1);
}
