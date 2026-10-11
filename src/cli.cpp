#include "cli.h"
#include "allocation.h"
#include <cot/cot.h>
#include <stdint.h>
#include <string.h>

static bool write_all(const cot_cli_io *io, void *stream,
	const unsigned char *data, size_t size)
{
	while (size) {
		size_t count = io->write(io->context, stream, data, size);

		if (!count || count > size)
			return false;
		data += count;
		size -= count;
	}
	return true;
}

static int diagnostic(const cot_cli_io *io, const char *text, int code)
{
	write_all(io, io->error, reinterpret_cast<const unsigned char *>(text),
		strlen(text));
	write_all(io, io->error, reinterpret_cast<const unsigned char *>("\n"),
		1);
	io->flush(io->context, io->error);
	return code;
}

int cot_cli_run(int argc, const char *const *argv, const cot_cli_io *io)
{
	const char *input_path = nullptr;
	const char *output_path = nullptr;
	bool options = true;
	bool help = false;
	bool encode = argc > 1 && !strcmp(argv[1], "encode");
	bool decode = argc > 1 && !strcmp(argv[1], "decode");

	/* Parse completely before opening files or consuming standard input. */
	if (argc == 2 && !strcmp(argv[1], "--help"))
		help = true;
	else if (!encode && !decode)
		return diagnostic(
			io, "argument error: expected encode or decode", 2);
	for (int i = 2; i < argc; ++i) {
		const char *arg = argv[i];

		if (options && !strcmp(arg, "--")) {
			options = false;
		} else if (options && !strcmp(arg, "--help")) {
			help = true;
		} else if (options &&
			   (!strcmp(arg, "-o") || !strcmp(arg, "--output"))) {
			if (output_path || i + 1 == argc)
				return diagnostic(
					io, "argument error: output option", 2);
			output_path = argv[++i];
		} else if (options && arg[0] == '-' && strcmp(arg, "-")) {
			return diagnostic(
				io, "argument error: unknown option", 2);
		} else {
			if (input_path)
				return diagnostic(
					io, "argument error: extra input", 2);
			input_path = arg;
		}
	}
	if (help) {
		const char *usage =
			"Usage: cot encode [INPUT|-] [-o OUTPUT|-]\n"
			"       cot decode [INPUT|-] [-o OUTPUT|-]\n"
			"Options: --output PATH, --help, --\n";

		if (!write_all(io, io->output,
			    reinterpret_cast<const unsigned char *>(usage),
			    strlen(usage)) ||
			io->flush(io->context, io->output))
			return diagnostic(io, "output I/O failure", 1);
		return 0;
	}

	/* Read through EOF and close input before conversion or truncation. */
	void *input = io->input;
	bool own_input = input_path && strcmp(input_path, "-");

	if (own_input)
		input = io->open(io->context, input_path, "rb");
	if (!input)
		return diagnostic(io, "input I/O failure: open", 1);
	unsigned char *data = nullptr;
	size_t size = 0;
	size_t capacity = 0;
	const char *failure = nullptr;

	for (;;) {
		unsigned char chunk[4096];
		size_t count =
			io->read(io->context, input, chunk, sizeof(chunk));

		if (count > SIZE_MAX - size) {
			failure = "allocation failure";
			break;
		}
		if (size + count > capacity) {
			size_t required = size + count;
			size_t next = capacity ? capacity : sizeof(chunk);

			while (next < required) {
				if (next > SIZE_MAX / 2) {
					next = required;
					break;
				}
				next *= 2;
			}
			unsigned char *replacement =
				static_cast<unsigned char *>(
					cot_allocate(next));

			if (!replacement) {
				failure = "allocation failure";
				break;
			}
			if (size)
				memcpy(replacement, data, size);
			cot_release(data);
			data = replacement;
			capacity = next;
		}
		if (count)
			memcpy(data + size, chunk, count);
		size += count;
		if (!count) {
			if (io->read_error(io->context, input))
				failure = "input I/O failure: read";
			break;
		}
	}
	if (own_input && io->close(io->context, input))
		failure = "input I/O failure: close";
	cot_decoded_event *owner = nullptr;
	cot_buffer buffer = {};

	/* Compose public operations; conversion belongs to the codec. */
	if (!failure) {
		cot_status status = cot_decode(
			encode ? COT_FORMAT_XML : COT_FORMAT_PROTOBUF, data,
			size, &owner);

		if (status == COT_STATUS_OK)
			status = cot_encode(
				encode ? COT_FORMAT_PROTOBUF : COT_FORMAT_XML,
				cot_decoded_event_get(owner), &buffer);
		if (status != COT_STATUS_OK)
			failure = cot_status_string(status);
	}
	cot_release(data);
	cot_decoded_event_destroy(owner);
	if (failure) {
		cot_buffer_destroy(&buffer);
		return diagnostic(io, failure, 1);
	}

	/* Only successful conversion permits preparing named output. */
	bool own_output = output_path && strcmp(output_path, "-");
	void *output = io->output;
	void *transaction = nullptr;

	if (own_output)
		output = io->temporary ? io->temporary(io->context,
			output_path, &transaction) : nullptr;
	if (!output) {
		failure = "output I/O failure: open";
	} else {
		if (!write_all(io, output, buffer.data, buffer.size))
			failure = "output I/O failure: write";
		if (io->flush(io->context, output))
			failure = "output I/O failure: flush";
		if (own_output && io->close(io->context, output))
			failure = "output I/O failure: close";
		if (own_output && !failure &&
			io->replace(io->context, transaction, output_path))
			failure = "output I/O failure: replace";
	}
	if (transaction && io->discard(io->context, transaction))
		failure = "output I/O failure: temporary cleanup";
	cot_buffer_destroy(&buffer);
	return failure ? diagnostic(io, failure, 1) : 0;
}
