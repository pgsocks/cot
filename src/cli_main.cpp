#include "cli.h"
#include <stdio.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <signal.h>
#endif

static void *open_file(void *, const char *path, const char *mode)
{
	return fopen(path, mode);
}

static size_t read_file(void *, void *stream, unsigned char *data, size_t size)
{
	return fread(data, 1, size, static_cast<FILE *>(stream));
}

static int read_error(void *, void *stream)
{
	return ferror(static_cast<FILE *>(stream));
}

static size_t write_file(
	void *, void *stream, const unsigned char *data, size_t size)
{
	return fwrite(data, 1, size, static_cast<FILE *>(stream));
}

static int flush_file(void *, void *stream)
{
	return fflush(static_cast<FILE *>(stream));
}

static int close_file(void *, void *stream)
{
	return fclose(static_cast<FILE *>(stream));
}

int main(int argc, char **argv)
{
#ifdef _WIN32
	if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
		_setmode(_fileno(stdout), _O_BINARY) == -1) {
		fputs("standard stream I/O failure\n", stderr);
		return 1;
	}
#else
	/* Report a closed consumer through the ordinary write error path. */
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		fputs("output I/O failure: signal setup\n", stderr);
		return 1;
	}
#endif
	cot_cli_io io = {nullptr, stdin, stdout, stderr, open_file, read_file,
		read_error, write_file, flush_file, close_file,
		cot_cli_temporary, cot_cli_replace, cot_cli_discard};

	return cot_cli_run(argc, argv, &io);
}
