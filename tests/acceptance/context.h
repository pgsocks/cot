#ifndef COT_ACCEPTANCE_CONTEXT_H
#define COT_ACCEPTANCE_CONTEXT_H

#include "support.h"
#include <gtest/gtest.h>
#include <cucumber-cpp/autodetect.hpp>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

using namespace cot_test;
using cucumber::ScenarioScope;

struct EventContext {
	cot_event caller = event();
	cot_detail detail{};
	cot_contact contact{};
	cot_group group{};
	cot_precision_location precision{};
	cot_status_detail status_detail{};
	cot_takv takv{};
	cot_track track{};
	std::deque<std::string> storage;
	std::vector<cot_extension> extensions;
	std::string input;
	std::string original_xml;
	std::string original_bytes;
	std::string argument;
	std::string message;
	cot_format input_format = COT_FORMAT_XML;
	owned_buffer output;
	owned_event owner;
	owned_event other;
	std::vector<cot_status> calls;
	cot_status encoded = COT_STATUS_OK;
	cot_status decoded = COT_STATUS_OK;
	const cot_event *saved_view = nullptr;
	cot_decoded_event *saved_owner = nullptr;
	uint8_t *saved_data = nullptr;
	size_t baseline = 0;
	bool failure_sweep = false;
	std::vector<const char *> status_pointers;
	std::vector<std::string> status_text;

	cot_text keep(const std::string &value);
	const cot_event *view() const;
	void encode(cot_format format, const cot_event *value);
	void decode(cot_format format, const std::string &value);
	void roundtrip(const std::vector<cot_format> &formats);
};

cot_format format(const std::string &name);
std::string string(cot_text value);
void same_core(const cot_event *actual, const cot_event *expected);
void same_event(const cot_event *actual, const cot_event *expected);
std::string canonical(const std::string &xml);
std::string fixture(const std::string &name);
std::string encoded_event(cot_format format, const cot_event &value);
void set_typed(EventContext &context, const std::string &name);
const void *typed(const cot_detail *detail, const std::string &name);
void replace(
	std::string &value, const std::string &from, const std::string &to);

#endif
