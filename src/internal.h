#ifndef COT_INTERNAL_H
#define COT_INTERNAL_H

#include <cot/cot.h>
#include "allocation.h"
#include <stddef.h>
#include <stdint.h>

struct cot_block;
struct cot_arena {
	cot_block *head;
	cot_status status;
};

struct cot_extension_node {
	cot_extension value;
	cot_extension_node *next;
};

struct cot_decoded_event {
	cot_arena arena;
	cot_diagnostic *diagnostic;
	cot_event event;
	cot_detail detail;
	cot_contact contact;
	cot_group group;
	cot_precision_location precision;
	cot_status_detail status;
	cot_takv takv;
	cot_track track;
	cot_extension_node *extensions;
	cot_extension_node *last_extension;
};

struct cot_writer {
	cot_diagnostic *diagnostic;
	uint8_t *data;
	size_t size;
	size_t capacity;
	cot_status status;
	bool counting;
};

void *cot_arena_allocate(cot_arena *arena, size_t size);
void cot_arena_destroy(cot_arena *arena);
cot_text cot_copy_text(cot_arena *arena, const char *data, size_t size);
bool cot_utf8(cot_text text, bool xml);
bool cot_text_equal(cot_text text, const char *literal);
void cot_write(cot_writer *writer, const void *data, size_t size);
void cot_write_literal(cot_writer *writer, const char *literal);
void cot_writer_destroy(cot_writer *writer);
cot_status cot_validate_event(const cot_event *event);
bool cot_parse_number(cot_text text, double *number);
bool cot_parse_uint(cot_text text, uint64_t maximum, uint64_t *number);
cot_status cot_parse_timestamp(cot_text text, uint64_t *time);
void cot_write_timestamp(cot_writer *writer, uint64_t time);
void cot_write_number(cot_writer *writer, double number);
void cot_write_uint(cot_writer *writer, uint64_t number);
bool cot_xml_point(const cot_point *point);
cot_status cot_parse_protobuf(
	cot_decoded_event *owner, const uint8_t *data, size_t size);
void cot_write_protobuf(cot_writer *writer, const cot_event *event);
cot_status cot_parse_xml(
	cot_decoded_event *owner, const uint8_t *data, size_t size);
void cot_write_xml(cot_writer *writer, const cot_event *event);
void cot_diagnostic_error(cot_diagnostic *diagnostic,
	cot_diagnostic_reason reason, const char *path, size_t offset);
void cot_diagnose_event(const cot_event *event, cot_format format,
	cot_diagnostic *diagnostic);

#endif
