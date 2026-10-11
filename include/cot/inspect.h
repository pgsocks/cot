#ifndef COT_INSPECT_H
#define COT_INSPECT_H

#include <cot/cot.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable native-field identities; valid IDs are 1 through field_count(). */
typedef enum cot_field_id {
	COT_FIELD_INVALID = 0,
	COT_FIELD_TYPE = 1,
	COT_FIELD_ACCESS = 2,
	COT_FIELD_QOS = 3,
	COT_FIELD_OPEX = 4,
	COT_FIELD_UID = 5,
	COT_FIELD_SEND_TIME_MS = 6,
	COT_FIELD_START_TIME_MS = 7,
	COT_FIELD_STALE_TIME_MS = 8,
	COT_FIELD_HOW = 9,
	COT_FIELD_LATITUDE = 10,
	COT_FIELD_LONGITUDE = 11,
	COT_FIELD_HAE = 12,
	COT_FIELD_CE = 13,
	COT_FIELD_LE = 14,
	COT_FIELD_CAVEAT = 15,
	COT_FIELD_RELEASABLE_TO = 16,
	COT_FIELD_CONTACT_ENDPOINT = 17,
	COT_FIELD_CONTACT_CALLSIGN = 18,
	COT_FIELD_CONTACT_ALTENDPOINTS = 19,
	COT_FIELD_GROUP_NAME = 20,
	COT_FIELD_GROUP_ROLE = 21,
	COT_FIELD_GEOPOINTSRC = 22,
	COT_FIELD_ALTSRC = 23,
	COT_FIELD_BATTERY = 24,
	COT_FIELD_TAKV_DEVICE = 25,
	COT_FIELD_TAKV_PLATFORM = 26,
	COT_FIELD_TAKV_OS = 27,
	COT_FIELD_TAKV_VERSION = 28,
	COT_FIELD_TRACK_SPEED = 29,
	COT_FIELD_TRACK_COURSE = 30,
	COT_FIELD_DETAIL_XML = 31,
	COT_FIELD_EXTENSIONS = 32
} cot_field_id;

/* Groups follow public struct boundaries; EVENT and POINT always exist. */
typedef enum cot_group_id {
	COT_GROUP_EVENT = 0,
	COT_GROUP_POINT = 1,
	COT_GROUP_DETAIL = 2,
	COT_GROUP_CONTACT = 3,
	COT_GROUP_GROUP = 4,
	COT_GROUP_PRECISION_LOCATION = 5,
	COT_GROUP_STATUS = 6,
	COT_GROUP_TAKV = 7,
	COT_GROUP_TRACK = 8
} cot_group_id;

/* Native value representation; UINT32 values also use value.uint_value. */
typedef enum cot_field_type {
	COT_VALUE_TEXT = 0,
	COT_VALUE_UINT64 = 1,
	COT_VALUE_UINT32 = 2,
	COT_VALUE_DOUBLE = 3,
	COT_VALUE_EXTENSIONS = 4
} cot_field_type;

/* Units describe interpretation, not validation or conversion policy. */
typedef enum cot_field_unit {
	COT_UNIT_NONE = 0,
	COT_UNIT_UTC_MILLISECONDS = 1,
	COT_UNIT_DEGREES = 2,
	COT_UNIT_METRES = 3,
	COT_UNIT_METRES_PER_SECOND = 4,
	COT_UNIT_PERCENT = 5
} cot_field_unit;

/*
 * Stable terminated native-type names: text, uint64, uint32, double,
 * extensions. Unknown enum representations return NULL. Returned storage is
 * immutable and lasts for the process; no allocation, I/O, or global mutation.
 * Independent calls and concurrent reads are safe.
 */
const char *cot_field_type_string(cot_field_type type);

/*
 * Same lifetime/error contract as field_type_string. Unit names are none,
 * utc_milliseconds, degrees, metres, metres_per_second, and percent.
 */
const char *cot_field_unit_string(cot_field_unit unit);

/*
 * Same lifetime/error contract as field_type_string. Return the canonical
 * group path: event, point, detail, detail.contact, detail.group,
 * detail.precision_location, detail.status, detail.takv, or detail.track.
 */
const char *cot_group_id_string(cot_group_id group);

/* Immutable process-lifetime metadata. path is canonical and terminated. */
typedef struct cot_field_info {
	cot_field_id id;
	const char *path;
	cot_field_type type;
	cot_field_unit unit;
	cot_group_id group;
	/* Nonzero means an empty value is semantically absent. */
	int empty_is_absent;
} cot_field_info;

/* Borrowed ordered extension collection, including empty entry payloads. */
typedef struct cot_extension_view {
	const cot_extension *data;
	size_t size;
} cot_extension_view;

/*
 * available is semantic availability, not original scalar wire presence.
 * When unavailable the union is all zero; type still identifies the field.
 * Text and extensions borrow event storage and share its lifetime.
 */
typedef struct cot_field_value {
	cot_field_type type;
	int available;
	union {
		cot_text text;
		uint64_t uint_value;
		double real;
		cot_extension_view extensions;
	} value;
} cot_field_value;

/* Number of catalog entries; no allocation, mutation, I/O, or global state. */
size_t cot_field_count(void);

/* Return static metadata; an invalid ID returns NULL. Never allocates. */
const cot_field_info *cot_field_get_info(cot_field_id id);

/*
 * Resolve a terminated canonical path once. NULL/unknown paths return INVALID.
 * Matching is exact and case-sensitive; no aliases or allocation.
 */
cot_field_id cot_field_resolve(const char *path);

/*
 * Read one field from a valid borrowed event, including caller-created events.
 * NULL arguments/invalid IDs return INVALID_ARGUMENT; a non-NULL out is reset
 * even on failure. Optional empty text and access="Undefined" are unavailable.
 * Required text (including empty) and numbers (including zero) are available
 * whenever their containing group exists. This does not validate the event.
 * Independent calls and concurrent reads of immutable inputs are safe.
 */
cot_status cot_field_get(const cot_event *event, cot_field_id id,
	cot_field_value *out);

/*
 * Query nested-message presence; NULL arguments/invalid IDs return
 * INVALID_ARGUMENT. A non-NULL out is reset on failure. Nonzero means present,
 * including present empty messages. No allocation or event validation.
 */
cot_status cot_group_is_present(const cot_event *event, cot_group_id group,
	int *out);

#ifdef __cplusplus
}
#endif

#endif
