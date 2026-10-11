#include "cli.h"
#include "allocation.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

struct output_transaction {
	char *path;
	bool published;
#ifndef _WIN32
	int descriptor;
	mode_t mode;
	dev_t device;
	ino_t inode;
	bool preserve_mode;
#endif
};

/* A named output is committed only after its stream has closed successfully.
 * The transaction owns the temporary pathname until discard, after either a
 * successful replacement or a failed write. No codec state participates.
 */
void *cot_cli_temporary(void *, const char *path, void **transaction)
{
	*transaction = nullptr;
	size_t length = std::strlen(path);
	size_t prefix = 0;

	for (size_t i = 0; i < length; ++i) {
		if (path[i] == '/')
			prefix = i + 1;
#ifdef _WIN32
		if (path[i] == '\\')
			prefix = i + 1;
#endif
	}
	if (prefix > SIZE_MAX - sizeof(output_transaction) - 32)
		return nullptr;
	void *storage = cot_allocate(sizeof(output_transaction) + prefix + 32);

	if (!storage)
		return nullptr;
	auto *token = new (storage) output_transaction{};

	token->path = reinterpret_cast<char *>(token + 1);
	std::memcpy(token->path, path, prefix);
#ifdef _WIN32
	DWORD attributes = GetFileAttributesA(path);

	if (attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & (FILE_ATTRIBUTE_DIRECTORY |
			FILE_ATTRIBUTE_REPARSE_POINT))) {
		cot_release(token);
		return nullptr;
	}
	char directory[MAX_PATH];
	char name[MAX_PATH];

	if (prefix >= sizeof(directory)) {
		cot_release(token);
		return nullptr;
	}
	std::memcpy(directory, path, prefix);
	directory[prefix] = 0;
	if (!prefix)
		std::strcpy(directory, ".");
	if (!GetTempFileNameA(directory, "cot", 0, name)) {
		cot_release(token);
		return nullptr;
	}
	if (std::strlen(name) >= prefix + 32) {
		DeleteFileA(name);
		cot_release(token);
		return nullptr;
	}
	std::strcpy(token->path, name);
	FILE *stream = std::fopen(token->path, "wb");
#else
	token->descriptor = -1;
	struct stat existing = {};
	bool present = lstat(path, &existing) == 0;

	if ((present && !S_ISREG(existing.st_mode)) ||
		(!present && errno != ENOENT)) {
		cot_release(token);
		return nullptr;
	}
	token->mode = existing.st_mode & 07777;
	token->preserve_mode = present;
	std::strcpy(token->path + prefix, ".cot-XXXXXX");
	token->descriptor = mkstemp(token->path);
	if (token->descriptor < 0) {
		cot_release(token);
		return nullptr;
	}
	struct stat created = {};

	if (fstat(token->descriptor, &created) ||
		fchmod(token->descriptor, 0600)) {
		close(token->descriptor);
		std::remove(token->path);
		cot_release(token);
		return nullptr;
	}
	token->device = created.st_dev;
	token->inode = created.st_ino;
	/* Keep unpublished bytes private; copy modes only at commit time. */
	int stream_descriptor = dup(token->descriptor);
	FILE *stream = stream_descriptor < 0
			       ? nullptr
			       : fdopen(stream_descriptor, "wb");

	if (!stream && stream_descriptor >= 0)
		close(stream_descriptor);
#endif
	if (!stream) {
		cot_cli_discard(nullptr, token);
		return nullptr;
	}
	*transaction = token;
	return stream;
}

int cot_cli_replace(void *, void *transaction, const char *path)
{
	auto *token = static_cast<output_transaction *>(transaction);

#ifdef _WIN32
	DWORD attributes = GetFileAttributesA(path);

	if (attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & (FILE_ATTRIBUTE_DIRECTORY |
			FILE_ATTRIBUTE_REPARSE_POINT)))
		return -1;
	if (!MoveFileExA(token->path, path, MOVEFILE_REPLACE_EXISTING))
		return -1;
#else
	struct stat existing = {};

	if (!lstat(path, &existing)) {
		if (!S_ISREG(existing.st_mode))
			return -1;
	} else if (errno != ENOENT) {
		return -1;
	}
	struct stat held = {}, named = {};

	if (fstat(token->descriptor, &held) || lstat(token->path, &named) ||
		!S_ISREG(named.st_mode) || held.st_dev != named.st_dev ||
		held.st_ino != named.st_ino)
		return -1;
	if (token->preserve_mode && fchmod(token->descriptor, token->mode))
		return -1;
	int descriptor = token->descriptor;

	token->descriptor = -1;
	if (close(descriptor) || std::rename(token->path, path))
		return -1;
#endif
	token->published = true;
	return 0;
}

int cot_cli_discard(void *, void *transaction)
{
	auto *token = static_cast<output_transaction *>(transaction);
	int status = 0;

	/* After rename the old pathname may already belong to another writer. */
	if (!token->published) {
#ifdef _WIN32
		if (std::remove(token->path) && errno != ENOENT)
			status = -1;
#else
		struct stat named = {};

		if (lstat(token->path, &named)) {
			if (errno != ENOENT)
				status = -1;
		} else if (named.st_dev != token->device ||
			   named.st_ino != token->inode) {
			status = -1;
		} else if (std::remove(token->path) && errno != ENOENT) {
			status = -1;
		}
#endif
	}
#ifndef _WIN32
	if (token->descriptor >= 0 && close(token->descriptor))
		status = -1;
#endif
	cot_release(token);
	return status;
}
