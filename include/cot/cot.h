#ifndef COT_COT_H
#define COT_COT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full behavioral contract and field mapping: docs/api.md. */
typedef enum cot_status {
	COT_STATUS_OK = 0,
	COT_STATUS_INVALID_ARGUMENT = 1,
	COT_STATUS_MALFORMED_MESSAGE = 2,
	COT_STATUS_UNSUPPORTED_CONTENT = 3,
	COT_STATUS_ALLOCATION_FAILURE = 4
} cot_status;

/* One complete XML event or one unframed official TakMessage payload. */
typedef enum cot_format {
	COT_FORMAT_XML = 0,
	COT_FORMAT_PROTOBUF = 1
} cot_format;

/* Counted UTF-8; no terminator required. NULL data requires size == 0. */
typedef struct cot_text {
	const char *data;
	size_t size;
} cot_text;

/* Counted opaque bytes; NULL data requires size == 0. */
typedef struct cot_bytes {
	const uint8_t *data;
	size_t size;
} cot_bytes;

/* WGS84 degrees; HAE and circular/linear error in metres. */
#define COT_POINT_UNKNOWN_METRES 999999.0

typedef struct cot_point {
	double latitude;
	double longitude;
	double hae;
	double ce;
	double le;
} cot_point;

/* Optional endpoint/altendpoints are empty when absent. */
typedef struct cot_contact {
	cot_text endpoint;
	cot_text callsign;
	cot_text altendpoints;
} cot_contact;

/* XML __group attributes. */
typedef struct cot_group {
	cot_text name;
	cot_text role;
} cot_group;

/* XML precisionlocation attributes, with upstream spelling. */
typedef struct cot_precision_location {
	cot_text geopointsrc;
	cot_text altsrc;
} cot_precision_location;

/* Battery percentage; the full uint32 protobuf domain is preserved. */
typedef struct cot_status_detail {
	uint32_t battery;
} cot_status_detail;

/* XML takv attributes. */
typedef struct cot_takv {
	cot_text device;
	cot_text platform;
	cot_text os;
	cot_text version;
} cot_takv;

/* Speed in metres/second; course in degrees clockwise from true north. */
typedef struct cot_track {
	double speed;
	double course;
} cot_track;

/* Registered upstream ID and uninterpreted data, including empty data. */
typedef struct cot_extension {
	uint32_t id;
	cot_bytes data;
} cot_extension;

/* NULL typed pointers mean absent; non-NULL preserves message presence. */
typedef struct cot_detail {
	/* XML children only: no detail wrapper or XML declaration. */
	cot_text xml;
	const cot_contact *contact;
	const cot_group *group;
	const cot_precision_location *precision_location;
	const cot_status_detail *status;
	const cot_takv *takv;
	const cot_track *track;
	/* Ordered entries; NULL requires extension_count == 0. */
	const cot_extension *extensions;
	size_t extension_count;
} cot_detail;

/* Every pinned CotEvent field. Times are UTC epoch milliseconds (uint64). */
typedef struct cot_event {
	cot_text type;
	cot_text access;
	cot_text qos;
	cot_text opex;
	cot_text uid;
	uint64_t send_time_ms;
	uint64_t start_time_ms;
	uint64_t stale_time_ms;
	cot_text how;
	cot_point point;
	const cot_detail *detail;
	cot_text caveat;
	cot_text releasable_to;
} cot_event;

/* Immutable decoded view owner; generated protobuf types remain private. */
typedef struct cot_decoded_event cot_decoded_event;

/* Library allocation. Initialize to {0}; release only with buffer_destroy. */
typedef struct cot_buffer {
	uint8_t *data;
	size_t size;
} cot_buffer;

/*
 * Borrow event and its reachable inputs for this call; do not mutate them.
 * out must be a valid empty buffer and must not alias any input.
 * On success it owns result bytes; on failure it remains empty.
 */
cot_status cot_encode(cot_format format, const cot_event *event,
	cot_buffer *out);

/*
 * Borrow data[0..size) for this call. NULL data is valid only at size zero.
 * out must point to a NULL owner and must not alias input bytes.
 * On success the new owner retains no pointers into data; failure leaves NULL.
 */
cot_status cot_decode(cot_format format, const uint8_t *data, size_t size,
	cot_decoded_event **out);

/* Machine-readable diagnostic reasons; status remains the error contract. */
typedef enum cot_diagnostic_reason {
	COT_DIAGNOSTIC_NONE = 0,
	COT_DIAGNOSTIC_INVALID_ARGUMENT = 1,
	COT_DIAGNOSTIC_MALFORMED_XML = 2,
	COT_DIAGNOSTIC_MALFORMED_PROTOBUF = 3,
	COT_DIAGNOSTIC_MISSING_REQUIRED = 4,
	COT_DIAGNOSTIC_INVALID_VALUE = 5,
	COT_DIAGNOSTIC_UNSUPPORTED_CONTENT = 6,
	COT_DIAGNOSTIC_ALLOCATION_FAILURE = 7
} cot_diagnostic_reason;

/* SIZE_MAX means no byte location is available (including encoding). */
#define COT_DIAGNOSTIC_NO_OFFSET SIZE_MAX

/*
 * A codec result with an optional first-error location and loss counters.
 * path is static storage naming a canonical field/group, or NULL when unknown.
 * byte_offset is zero-based in the supplied decode input, or NO_OFFSET.
 * Unknown Protobuf counters count discarded wire fields and their complete
 * tag/value bytes. A whole unknown group counts once. On decode success they
 * cover the complete input; on failure they cover only the examined prefix.
 * Encoding resets both counters to zero. No diagnostic owns any resources.
 */
typedef struct cot_diagnostic {
	cot_diagnostic_reason reason;
	const char *path;
	size_t byte_offset;
	size_t unknown_protobuf_fields;
	size_t unknown_protobuf_bytes;
} cot_diagnostic;

/*
 * Same contracts as encode/decode, with optional diagnostic output, reset on
 * entry on every call. Diagnostic storage must not alias inputs or outputs.
 * Successful calls have reason NONE. Specific field and location information
 * is supplied where known; status always determines success or failure.
 */
cot_status cot_encode_ex(cot_format format, const cot_event *event,
	cot_buffer *out, cot_diagnostic *diagnostic);
cot_status cot_decode_ex(cot_format format, const uint8_t *data, size_t size,
	cot_decoded_event **out, cot_diagnostic *diagnostic);

/* Borrow an immutable view until owner destruction; NULL owner returns NULL. */
const cot_event *cot_decoded_event_get(const cot_decoded_event *owner);

/* NULL is harmless. Release once; all views into this owner are invalidated. */
void cot_decoded_event_destroy(cot_decoded_event *owner);

/* NULL is harmless. Releases an owned buffer, resets to {0}; repeatable. */
void cot_buffer_destroy(cot_buffer *buffer);

/* Static English text; unknown values return "unknown status". */
const char *cot_status_string(cot_status status);

#ifdef __cplusplus
}
#endif

#endif
