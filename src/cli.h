#ifndef COT_CLI_H
#define COT_CLI_H

#include <stddef.h>

/* Private I/O boundary: streams are borrowed; opened files are owned. */
struct cot_cli_io {
	void *context;
	void *input;
	void *output;
	void *error;
	void *(*open)(void *, const char *, const char *);
	size_t (*read)(void *, void *, unsigned char *, size_t);
	int (*read_error)(void *, void *);
	size_t (*write)(void *, void *, const unsigned char *, size_t);
	int (*flush)(void *, void *);
	int (*close)(void *, void *);
	/* temporary returns an owned stream and transaction on success.
	 * Close the stream before replace. discard always releases the token,
	 * removing an unpublished temporary file; it is required after replace.
	 */
	void *(*temporary)(void *, const char *, void **);
	int (*replace)(void *, void *, const char *);
	int (*discard)(void *, void *);
};

void *cot_cli_temporary(void *, const char *path, void **transaction);
int cot_cli_replace(void *, void *transaction, const char *path);
int cot_cli_discard(void *, void *transaction);

int cot_cli_run(int argc, const char *const *argv, const cot_cli_io *io);

#endif
