#include <cot/inspect.hpp>
#include "internal.h"

#include <cmath>
#include <cstring>

namespace
{

struct field_entry {
	cot_field_info info;
	size_t offset;
};

const field_entry fields[] = {
	{{COT_FIELD_TYPE, "type",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_EVENT, 0}, offsetof(cot_event, type)},
	{{COT_FIELD_ACCESS, "access",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_EVENT, 1}, offsetof(cot_event, access)},
	{{COT_FIELD_QOS, "qos",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_EVENT, 1}, offsetof(cot_event, qos)},
	{{COT_FIELD_OPEX, "opex",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_EVENT, 1}, offsetof(cot_event, opex)},
	{{COT_FIELD_UID, "uid",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_EVENT, 0}, offsetof(cot_event, uid)},
	{{COT_FIELD_SEND_TIME_MS, "send_time_ms",
		  COT_VALUE_UINT64, COT_UNIT_UTC_MILLISECONDS,
		  COT_GROUP_EVENT, 0}, offsetof(cot_event, send_time_ms)},
	{{COT_FIELD_START_TIME_MS, "start_time_ms",
		  COT_VALUE_UINT64, COT_UNIT_UTC_MILLISECONDS,
		  COT_GROUP_EVENT, 0}, offsetof(cot_event, start_time_ms)},
	{{COT_FIELD_STALE_TIME_MS, "stale_time_ms",
		  COT_VALUE_UINT64, COT_UNIT_UTC_MILLISECONDS,
		  COT_GROUP_EVENT, 0}, offsetof(cot_event, stale_time_ms)},
	{{COT_FIELD_HOW, "how",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_EVENT, 0}, offsetof(cot_event, how)},
	{{COT_FIELD_LATITUDE, "point.latitude",
		  COT_VALUE_DOUBLE, COT_UNIT_DEGREES,
		  COT_GROUP_POINT, 0}, offsetof(cot_point, latitude)},
	{{COT_FIELD_LONGITUDE, "point.longitude",
		  COT_VALUE_DOUBLE, COT_UNIT_DEGREES,
		  COT_GROUP_POINT, 0}, offsetof(cot_point, longitude)},
	{{COT_FIELD_HAE, "point.hae",
		  COT_VALUE_DOUBLE, COT_UNIT_METRES,
		  COT_GROUP_POINT, 0}, offsetof(cot_point, hae)},
	{{COT_FIELD_CE, "point.ce",
		  COT_VALUE_DOUBLE, COT_UNIT_METRES,
		  COT_GROUP_POINT, 0}, offsetof(cot_point, ce)},
	{{COT_FIELD_LE, "point.le",
		  COT_VALUE_DOUBLE, COT_UNIT_METRES,
		  COT_GROUP_POINT, 0}, offsetof(cot_point, le)},
	{{COT_FIELD_CAVEAT, "caveat",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_EVENT, 1}, offsetof(cot_event, caveat)},
	{{COT_FIELD_RELEASABLE_TO, "releasable_to",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_EVENT, 1}, offsetof(cot_event, releasable_to)},
	{{COT_FIELD_CONTACT_ENDPOINT, "detail.contact.endpoint",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_CONTACT, 1}, offsetof(cot_contact, endpoint)},
	{{COT_FIELD_CONTACT_CALLSIGN, "detail.contact.callsign",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_CONTACT, 0}, offsetof(cot_contact, callsign)},
	{{COT_FIELD_CONTACT_ALTENDPOINTS, "detail.contact.altendpoints",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_CONTACT, 1}, offsetof(cot_contact, altendpoints)},
	{{COT_FIELD_GROUP_NAME, "detail.group.name",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_GROUP, 0}, offsetof(cot_group, name)},
	{{COT_FIELD_GROUP_ROLE, "detail.group.role",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_GROUP, 0}, offsetof(cot_group, role)},
	{{COT_FIELD_GEOPOINTSRC, "detail.precision_location.geopointsrc",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_PRECISION_LOCATION, 0},
		offsetof(cot_precision_location, geopointsrc)},
	{{COT_FIELD_ALTSRC, "detail.precision_location.altsrc",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_PRECISION_LOCATION, 0}, offsetof(cot_precision_location, altsrc)},
	{{COT_FIELD_BATTERY, "detail.status.battery",
		  COT_VALUE_UINT32, COT_UNIT_PERCENT,
		  COT_GROUP_STATUS, 0}, offsetof(cot_status_detail, battery)},
	{{COT_FIELD_TAKV_DEVICE, "detail.takv.device",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_TAKV, 0}, offsetof(cot_takv, device)},
	{{COT_FIELD_TAKV_PLATFORM, "detail.takv.platform",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_TAKV, 0}, offsetof(cot_takv, platform)},
	{{COT_FIELD_TAKV_OS, "detail.takv.os",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_TAKV, 0}, offsetof(cot_takv, os)},
	{{COT_FIELD_TAKV_VERSION, "detail.takv.version",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_TAKV, 0}, offsetof(cot_takv, version)},
	{{COT_FIELD_TRACK_SPEED, "detail.track.speed",
		  COT_VALUE_DOUBLE, COT_UNIT_METRES_PER_SECOND,
		  COT_GROUP_TRACK, 0}, offsetof(cot_track, speed)},
	{{COT_FIELD_TRACK_COURSE, "detail.track.course",
		  COT_VALUE_DOUBLE, COT_UNIT_DEGREES,
		  COT_GROUP_TRACK, 0}, offsetof(cot_track, course)},
	{{COT_FIELD_DETAIL_XML, "detail.xml",
		  COT_VALUE_TEXT, COT_UNIT_NONE,
		  COT_GROUP_DETAIL, 1}, offsetof(cot_detail, xml)},
	{{COT_FIELD_EXTENSIONS, "detail.extensions",
		  COT_VALUE_EXTENSIONS, COT_UNIT_NONE,
		  COT_GROUP_DETAIL, 1}, offsetof(cot_detail, extensions)},
};

static const cot_field_info *field_info(const cot_field_id *id)
{
	/* Out-of-range enum representations can be supplied through the C ABI. */
	for (const field_entry &entry : fields)
		if (!memcmp(id, &entry.info.id, sizeof(*id)))
			return &entry.info;
	return nullptr;
}

static const void *group_object(const cot_event *event, cot_group_id group)
{
	const cot_detail *detail = event->detail;

	switch (group) {
	case COT_GROUP_EVENT:
		return event;
	case COT_GROUP_POINT:
		return &event->point;
	case COT_GROUP_DETAIL:
		return detail;
	case COT_GROUP_CONTACT:
		return detail ? detail->contact : nullptr;
	case COT_GROUP_GROUP:
		return detail ? detail->group : nullptr;
	case COT_GROUP_PRECISION_LOCATION:
		return detail ? detail->precision_location : nullptr;
	case COT_GROUP_STATUS:
		return detail ? detail->status : nullptr;
	case COT_GROUP_TAKV:
		return detail ? detail->takv : nullptr;
	case COT_GROUP_TRACK:
		return detail ? detail->track : nullptr;
	}
	return nullptr;
}

} // namespace

const char *cot_field_type_string(cot_field_type type)
{
	const cot_field_type known[] = {COT_VALUE_TEXT, COT_VALUE_UINT64,
		COT_VALUE_UINT32, COT_VALUE_DOUBLE, COT_VALUE_EXTENSIONS};
	const char *const labels[] = {
		"text", "uint64", "uint32", "double", "extensions"};

	for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
		if (!memcmp(&type, &known[i], sizeof(type)))
			return labels[i];
	return nullptr;
}

const char *cot_field_unit_string(cot_field_unit unit)
{
	const cot_field_unit known[] = {COT_UNIT_NONE,
		COT_UNIT_UTC_MILLISECONDS, COT_UNIT_DEGREES, COT_UNIT_METRES,
		COT_UNIT_METRES_PER_SECOND, COT_UNIT_PERCENT};
	const char *const labels[] = {"none", "utc_milliseconds", "degrees",
		"metres", "metres_per_second", "percent"};

	for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
		if (!memcmp(&unit, &known[i], sizeof(unit)))
			return labels[i];
	return nullptr;
}

const char *cot_group_id_string(cot_group_id group)
{
	const cot_group_id known[] = {COT_GROUP_EVENT, COT_GROUP_POINT,
		COT_GROUP_DETAIL, COT_GROUP_CONTACT, COT_GROUP_GROUP,
		COT_GROUP_PRECISION_LOCATION, COT_GROUP_STATUS, COT_GROUP_TAKV,
		COT_GROUP_TRACK};
	const char *const labels[] = {"event", "point", "detail",
		"detail.contact", "detail.group", "detail.precision_location",
		"detail.status", "detail.takv", "detail.track"};

	for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
		if (!memcmp(&group, &known[i], sizeof(group)))
			return labels[i];
	return nullptr;
}

size_t cot_field_count(void)
{
	return sizeof(fields) / sizeof(fields[0]);
}

const cot_field_info *cot_field_get_info(cot_field_id id)
{
	return field_info(&id);
}

cot_field_id cot_field_resolve(const char *path)
{
	if (path)
		for (const field_entry &entry : fields)
			if (!strcmp(path, entry.info.path))
				return entry.info.id;
	return COT_FIELD_INVALID;
}

cot_status cot_field_get(const cot_event *event, cot_field_id id,
	cot_field_value *out)
{
	if (out)
		*out = {};
	const cot_field_info *info = field_info(&id);

	if (!event || !out || !info)
		return COT_STATUS_INVALID_ARGUMENT;
	out->type = info->type;
	const void *base = group_object(event, info->group);

	if (!base)
		return COT_STATUS_OK;
	const field_entry *entry = &fields[static_cast<unsigned>(id) - 1];
	const uint8_t *source = static_cast<const uint8_t *>(base) +
				entry->offset;

	out->available = 1;
	switch (info->type) {
	case COT_VALUE_TEXT: {
		cot_text text;

		memcpy(&text, source, sizeof(text));
		if ((info->empty_is_absent && !text.size) ||
			(id == COT_FIELD_ACCESS && text.data &&
				cot_text_equal(text, "Undefined"))) {
			out->available = 0;
		} else {
			out->value.text = text;
		}
		break;
	}
	case COT_VALUE_UINT64:
		memcpy(&out->value.uint_value, source, sizeof(uint64_t));
		break;
	case COT_VALUE_UINT32: {
		uint32_t value;

		memcpy(&value, source, sizeof(value));
		out->value.uint_value = value;
		break;
	}
	case COT_VALUE_DOUBLE:
		memcpy(&out->value.real, source, sizeof(double));
		break;
	case COT_VALUE_EXTENSIONS:
		if (event->detail->extension_count)
			out->value.extensions = {event->detail->extensions,
				event->detail->extension_count};
		else
			out->available = 0;
		break;
	}
	return COT_STATUS_OK;
}

cot_status cot_group_is_present(const cot_event *event, cot_group_id group,
	int *out)
{
	if (out)
		*out = 0;
	bool known = false;

	for (const field_entry &entry : fields)
		if (!memcmp(&group, &entry.info.group, sizeof(group)))
			known = true;
	if (!event || !out || !known)
		return COT_STATUS_INVALID_ARGUMENT;
	*out = group_object(event, group) != nullptr;
	return COT_STATUS_OK;
}

void cot_diagnostic_error(cot_diagnostic *diagnostic,
	cot_diagnostic_reason reason, const char *path, size_t offset)
{
	if (!diagnostic || diagnostic->reason != COT_DIAGNOSTIC_NONE)
		return;
	diagnostic->reason = reason;
	diagnostic->path = path;
	diagnostic->byte_offset = offset;
}

void cot_diagnose_event(const cot_event *event, cot_format format,
	cot_diagnostic *diagnostic)
{
	if (!diagnostic)
		return;
	for (const field_entry &entry : fields) {
		cot_field_value value{};

		cot_field_get(event, entry.info.id, &value);
		if (!value.available)
			continue;
		/* XML may suppress typed details in favour of residual elements. */
		if (format == COT_FORMAT_XML &&
			entry.info.group != COT_GROUP_EVENT &&
			entry.info.group != COT_GROUP_POINT &&
			entry.info.id != COT_FIELD_EXTENSIONS)
			continue;
		bool invalid = false;

		if (value.type == COT_VALUE_TEXT)
			invalid = !cot_utf8(value.value.text, false);
		if (format == COT_FORMAT_XML) {
			if (value.type == COT_VALUE_TEXT)
				invalid |= !cot_utf8(value.value.text, true);
			if (entry.info.id == COT_FIELD_TYPE ||
				entry.info.id == COT_FIELD_UID ||
				entry.info.id == COT_FIELD_HOW)
				invalid |= !value.value.text.size;
			if (value.type == COT_VALUE_UINT64)
				invalid |= value.value.uint_value >
					   253402300799999ULL;
			if (value.type == COT_VALUE_DOUBLE) {
				double n = value.value.real;

				invalid |= !std::isfinite(n);
				if (entry.info.id == COT_FIELD_LATITUDE)
					invalid |= n < -90 || n > 90;
				if (entry.info.id == COT_FIELD_LONGITUDE)
					invalid |= n < -180 || n > 180;
				if (entry.info.id == COT_FIELD_CE ||
					entry.info.id == COT_FIELD_LE)
					invalid |= n < 0;
			}
			if (value.type == COT_VALUE_EXTENSIONS)
				invalid = true;
		}
		if (invalid) {
			cot_diagnostic_error(diagnostic,
				COT_DIAGNOSTIC_INVALID_VALUE, entry.info.path,
				COT_DIAGNOSTIC_NO_OFFSET);
			return;
		}
	}
}
