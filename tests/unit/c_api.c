#include <cot/inspect.h>
#include <cot/cot.h>

_Static_assert(sizeof(((cot_event *)0)->send_time_ms) == sizeof(uint64_t),
	"timestamps use uint64_t");
_Static_assert(sizeof(((cot_extension *)0)->id) == sizeof(uint32_t),
	"extension IDs use uint32_t");

cot_status cot_test_c_convert(const uint8_t *data, size_t size,
	cot_buffer *output)
{
	cot_decoded_event *owner = NULL;
	cot_status status = cot_decode(COT_FORMAT_XML, data, size, &owner);

	if (status != COT_STATUS_OK)
		return status;
	const cot_event *view = cot_decoded_event_get(owner);

	status = cot_encode(COT_FORMAT_PROTOBUF, view, output);
	cot_decoded_event_destroy(owner);
	return status;
}

void cot_test_c_cleanup(cot_buffer *buffer)
{
	cot_buffer_destroy(buffer);
	cot_buffer_destroy(buffer);
	cot_decoded_event_destroy(NULL);
}

/* C can pass enum values outside the named enumerators to the C ABI. */
cot_status cot_test_c_encode_format(int format, const cot_event *event,
	cot_buffer *output)
{
	return cot_encode((cot_format)format, event, output);
}

cot_status cot_test_c_decode_format(int format, const uint8_t *data,
	size_t size, cot_decoded_event **output)
{
	return cot_decode((cot_format)format, data, size, output);
}

const char *cot_test_c_status(int status)
{
	return cot_status_string((cot_status)status);
}

cot_status cot_test_c_inspect(const cot_event *event, cot_field_value *value)
{
	cot_field_id id = cot_field_resolve("send_time_ms");

	return cot_field_get(event, id, value);
}

cot_status cot_test_c_field_id(int id, const cot_event *event,
	cot_field_value *value)
{
	return cot_field_get(event, (cot_field_id)id, value);
}

cot_status cot_test_c_group_id(int group, const cot_event *event, int *present)
{
	return cot_group_is_present(event, (cot_group_id)group, present);
}

const char *cot_test_c_field_type_string(int type)
{
	return cot_field_type_string((cot_field_type)type);
}

const char *cot_test_c_field_unit_string(int unit)
{
	return cot_field_unit_string((cot_field_unit)unit);
}

const char *cot_test_c_group_id_string(int group)
{
	return cot_group_id_string((cot_group_id)group);
}
