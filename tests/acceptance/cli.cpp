#include "context.h"
#include "cli.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

struct Stream {
	std::string data;
	size_t offset = 0;
	bool error = false;
	bool closed = false;
	bool writable = false;
};

struct CliContext {
	fs::path directory;
	std::map<std::string, Stream> files;
	Stream input;
	Stream output;
	Stream error;
	std::string failure;
	std::string source;
	std::string expected;
	std::string command;
	std::string target;
	std::string mode;
	int status = -1;
	unsigned opens = 0;
	unsigned closes = 0;
	bool inject = false;
	bool short_writes = false;
	bool waited = false;
	bool opened_output = false;
	size_t allocation_baseline = 0;
	std::thread worker;
	int helper_status = -1;

	CliContext()
	{
		static unsigned serial = 0;

		directory = fs::temp_directory_path() /
			    ("cot-acceptance-" +
				    std::to_string(std::chrono::
						    high_resolution_clock::now()
							    .time_since_epoch()
							    .count()) +
				    "-" + std::to_string(++serial));
		fs::create_directories(directory);
	}

	~CliContext()
	{
		if (worker.joinable()) {
			std::ofstream(directory / "close-pipe").close();
			worker.join();
		}
		std::error_code ignored;

		fs::remove_all(directory, ignored);
	}

	void file(const std::string &name, const std::string &data)
	{
		files[name].data = data;
		std::ofstream stream(directory / name, std::ios::binary);

		stream.write(data.data(), data.size());
	}

	std::string read(const std::string &name)
	{
		if (mode == "fake")
			return files.at(name).data;
		std::ifstream stream(directory / name, std::ios::binary);

		return {std::istreambuf_iterator<char>(stream), {}};
	}
};

static void *open_stream(void *state, const char *path, const char *mode)
{
	auto &c = *static_cast<CliContext *>(state);

	if (mode[0] == 'r') {
		if (!c.files.count(path))
			return nullptr;
		++c.opens;
		return &c.files.at(path);
	}
	if (c.failure == "file creation failure")
		return nullptr;
	Stream &stream = c.files[path];

	stream.data.clear();
	stream.writable = true;
	++c.opens;
	return &stream;
}

static size_t read_stream(
	void *state, void *handle, unsigned char *data, size_t size)
{
	auto &c = *static_cast<CliContext *>(state);
	auto &stream = *static_cast<Stream *>(handle);

	if (c.failure == "reading input.xml fails before EOF") {
		stream.error = true;
		return 0;
	}
	size_t count = std::min(size, stream.data.size() - stream.offset);

	std::copy_n(stream.data.data() + stream.offset, count, data);
	stream.offset += count;
	return count;
}

static int stream_error(void *, void *handle)
{
	return static_cast<Stream *>(handle)->error;
}

static size_t write_stream(
	void *state, void *handle, const unsigned char *data, size_t size)
{
	auto &c = *static_cast<CliContext *>(state);
	auto &stream = *static_cast<Stream *>(handle);

	if (&stream != &c.error) {
		if ((c.failure == "write failure" ||
			    c.failure == "a closed pipe consumer") &&
			!stream.data.empty())
			return 0;
		if (c.short_writes || c.failure == "write failure" ||
			c.failure == "a closed pipe consumer")
			size = std::min(size, size_t{3});
	}
	stream.data.append(reinterpret_cast<const char *>(data), size);
	return size;
}

static int flush_stream(void *state, void *handle)
{
	auto &c = *static_cast<CliContext *>(state);

	return handle != &c.error && c.failure == "flush failure" ? -1 : 0;
}

static int close_stream(void *state, void *handle)
{
	auto &c = *static_cast<CliContext *>(state);
	auto &stream = *static_cast<Stream *>(handle);

	EXPECT_FALSE(stream.closed);
	stream.closed = true;
	++c.closes;
	return (stream.writable && c.failure == "file close failure") ||
			       (!stream.writable &&
				       c.failure == "closing input.xml fails "
						    "after EOF")
		       ? -1
		       : 0;
}

static void *temporary_stream(void *state, const char *, void **token)
{
	auto &c = *static_cast<CliContext *>(state);

	*token = nullptr;
	if (c.failure == "file creation failure")
		return nullptr;
	auto *name = new std::string(".cot-output.tmp");
	Stream &stream = c.files[*name];

	stream.writable = true;
	++c.opens;
	*token = name;
	return &stream;
}

static int replace_stream(void *state, void *token, const char *path)
{
	auto &c = *static_cast<CliContext *>(state);
	auto *name = static_cast<std::string *>(token);

	if (c.failure == "replacement failure" ||
		c.failure == "cleanup failure")
		return -1;
	c.files[path] = c.files.at(*name);
	c.files.erase(*name);
	return 0;
}

static int discard_stream(void *state, void *token)
{
	auto &c = *static_cast<CliContext *>(state);
	auto *name = static_cast<std::string *>(token);

	c.files.erase(*name);
	delete name;
	return c.failure == "cleanup failure" ? -1 : 0;
}

static std::vector<std::string> arguments(const std::string &command)
{
	std::istringstream words(command);
	std::vector<std::string> result;
	std::string word;

	while (words >> word)
		result.push_back(word);
	return result;
}

static std::string shell_quote(const std::string &value)
{
#ifdef _WIN32
	return "\"" + value + "\"";
#else
	std::string quoted = "'";

	for (char ch : value)
		quoted += ch == '\'' ? "'\\''" : std::string(1, ch);
	return quoted + "'";
#endif
}

static void collect(CliContext &c)
{
	c.output.data = c.read("stdout");
	c.error.data = c.read("stderr");
	std::ifstream status(c.directory / "status");
	std::string waited;
	std::string opened;

	status >> c.status >> waited >> opened;
	ASSERT_TRUE(status.good());
	c.waited = waited == "true";
	c.opened_output = opened == "true";
}

static void run(CliContext &c, const std::string &command,
	const std::string &mode = "normal")
{
	c.command = command;
	auto args = arguments(command);

	ASSERT_FALSE(args.empty());
	if (c.inject || !c.failure.empty() || c.short_writes) {
		c.mode = "fake";
		std::vector<const char *> argv;

		for (const auto &arg : args)
			argv.push_back(arg.c_str());
		cot_cli_io io = {&c, &c.input, &c.output, &c.error, open_stream,
			read_stream, stream_error, write_stream, flush_stream,
			close_stream, temporary_stream, replace_stream,
			discard_stream};

		c.allocation_baseline = live_allocations();
		allocation_failure failure(c.inject ? 2 : 0);

		c.status = cot_cli_run(argv.size(), argv.data(), &io);
		EXPECT_EQ(c.files.count(".cot-output.tmp"), 0u);
		EXPECT_EQ(live_allocations(), c.allocation_baseline);
		EXPECT_EQ(c.opens, c.closes);
		return;
	}
#ifdef COT_CLI_PATH
	c.mode = mode;
	std::ofstream request(c.directory / "arguments", std::ios::binary);

	for (size_t i = 1; i < args.size(); ++i)
		request << args[i] << '\n';
	request.close();
	std::ofstream input(c.directory / "stdin", std::ios::binary);

	input.write(c.input.data.data(), c.input.data.size());
	input.close();
	std::string invocation = shell_quote(COT_RUBY_PATH) + " " +
				 shell_quote(COT_PROCESS_HELPER) + " " +
				 shell_quote(c.directory.string()) + " " +
				 shell_quote(mode) + " " +
				 shell_quote(COT_CLI_PATH);

	if (mode == "eof") {
		c.worker = std::thread([&c, invocation]() {
			c.helper_status = std::system(invocation.c_str());
		});
		auto deadline = std::chrono::steady_clock::now() +
				std::chrono::seconds(5);

		while (!fs::exists(c.directory / "ready") &&
			std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(
				std::chrono::milliseconds(5));
		ASSERT_TRUE(fs::exists(c.directory / "ready"));
		std::ifstream ready(c.directory / "ready");
		std::string waited;
		std::string opened;

		ready >> waited >> opened;
		c.waited = waited == "true";
		c.opened_output = opened == "true";
		return;
	}
	ASSERT_EQ(std::system(invocation.c_str()), 0);
	collect(c);
#else
	FAIL() << "CLI scenarios require BUILD_CLI=ON";
#endif
}

static void represented(
	const std::string &actual, cot_format kind, const std::string &expected)
{
	owned_event a;
	owned_event b;

	ASSERT_EQ(decode(kind, actual, a), COT_STATUS_OK);
	ASSERT_EQ(decode(COT_FORMAT_XML, expected, b), COT_STATUS_OK);
	same_event(a.get(), b.get());
}

namespace
{

GIVEN("^stdin is open and has not reached EOF$")
{
	ScenarioScope<CliContext> c;

	c->mode = "help";
}

WHEN("^I run \"([^\"]*)\"(?: through EOF)?$")
{
	REGEX_PARAM(std::string, command);
	ScenarioScope<CliContext> c;

	run(*c, command, c->mode.empty() ? "normal" : c->mode);
}

THEN("^it exits ([012])(?: without waiting for EOF)?$")
{
	REGEX_PARAM(int, status);
	ScenarioScope<CliContext> c;

	EXPECT_EQ(c->status, status) << c->error.data;
	if (c->mode == "help")
		EXPECT_TRUE(c->waited);
}

THEN("^stdout contains usage for (cot|encode|decode)$")
{
	REGEX_PARAM(std::string, scope);
	ScenarioScope<CliContext> c;

	EXPECT_NE(c->output.data.find("Usage:"), std::string::npos);
	EXPECT_NE(c->output.data.find(scope), std::string::npos);
}

THEN("^stderr is empty$")
{
	ScenarioScope<CliContext> c;

	EXPECT_TRUE(c->error.data.empty()) << c->error.data;
}

THEN("^no input or output file is opened$")
{
	ScenarioScope<CliContext> c;
	CliContext probe;

	/* The same command also runs against an observable I/O boundary. */
	probe.short_writes = true;
	run(probe, c->command);
	EXPECT_EQ(probe.status, 0);
	EXPECT_EQ(probe.opens, 0U);
	EXPECT_EQ(probe.input.offset, 0U);
}

GIVEN("^one valid (XML|Protobuf) event available as \"input.dat\" or on stdin$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<CliContext> c;
	cot_event value = event();
	cot_detail detail{};

	value.detail = &detail;
	c->expected = xml();
	c->source = kind == "XML" ? c->expected
				  : encoded_event(COT_FORMAT_PROTOBUF, value);
	c->input.data = c->source;
	c->file("input.dat", c->source);
}

THEN("^(stdout|output.dat) contains one complete (XML|Protobuf) event with the "
	"same represented fields$")
{
	REGEX_PARAM(std::string, destination);
	REGEX_PARAM(std::string, kind);
	ScenarioScope<CliContext> c;

	c->target = destination;
	represented(
		destination == "stdout" ? c->output.data : c->read(destination),
		format(kind), c->expected);
}

THEN("^stdout contains only result bytes when selected and is otherwise empty$")
{
	ScenarioScope<CliContext> c;

	if (c->target == "stdout") {
		cot_format kind = c->command.find("encode") != std::string::npos
					  ? COT_FORMAT_PROTOBUF
					  : COT_FORMAT_XML;
		owned_event expected;

		ASSERT_EQ(decode(COT_FORMAT_XML, c->expected, expected),
			COT_STATUS_OK);
		EXPECT_EQ(c->output.data, encoded_event(kind, *expected.get()));
	} else {
		EXPECT_TRUE(c->output.data.empty());
	}
}

GIVEN("^\"([^\"]*)\" contains a valid XML event$")
{
	REGEX_PARAM(std::string, path);
	ScenarioScope<CliContext> c;

	c->expected = xml();
	c->file(path, c->expected);
}

THEN("^\"([^\"]*)\" contains the corresponding unframed Protobuf event$")
{
	REGEX_PARAM(std::string, path);
	ScenarioScope<CliContext> c;

	represented(c->read(path), COT_FORMAT_PROTOBUF, c->expected);
}

THEN("^stdout and stderr are empty$")
{
	ScenarioScope<CliContext> c;

	EXPECT_TRUE(c->output.data.empty());
	EXPECT_TRUE(c->error.data.empty());
}

GIVEN("^a pipe writer has sent one complete valid XML event without closing "
	"stdin$")
{
	ScenarioScope<CliContext> c;

	c->input.data = c->expected = xml();
}

WHEN("^I start \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, command);
	ScenarioScope<CliContext> c;

	run(*c, command, "eof");
}

THEN("^the command waits for EOF$")
{
	ScenarioScope<CliContext> c;

	EXPECT_TRUE(c->waited);
}

THEN("^stdout is empty and \"result.pb\" has not been created$")
{
	ScenarioScope<CliContext> c;

	EXPECT_TRUE(c->read("before.stdout").empty());
	EXPECT_FALSE(fs::exists(c->directory / "result.pb"));
	EXPECT_FALSE(c->opened_output);
}

WHEN("^the writer closes the pipe$")
{
	ScenarioScope<CliContext> c;

	ASSERT_TRUE(c->worker.joinable());
	std::ofstream(c->directory / "close-pipe").close();
	c->worker.join();
	ASSERT_EQ(c->helper_status, 0);
	collect(*c);
}

THEN("^the command exits 0 with the corresponding Protobuf in \"result.pb\"$")
{
	ScenarioScope<CliContext> c;

	EXPECT_EQ(c->status, 0);
	represented(c->read("result.pb"), COT_FORMAT_PROTOBUF, c->expected);
}

GIVEN("^stdin contains a valid XML event followed by another XML event$")
{
	ScenarioScope<CliContext> c;

	c->input.data = xml() + xml();
}

GIVEN("^\"([^\"]*)\" contains the bytes \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, path);
	REGEX_PARAM(std::string, data);
	ScenarioScope<CliContext> c;

	c->file(path, data);
}

THEN("^stderr identifies (a malformed message|unsupported content|the argument "
	"error|allocation failure|the input I/O failure|the output I/O"
	" failure)$")
{
	REGEX_PARAM(std::string, cause);
	ScenarioScope<CliContext> c;
	std::string expected =
		cause == "a malformed message"	    ? "malformed message"
		: cause == "the argument error"	    ? "argument error"
		: cause == "the input I/O failure"  ? "input I/O failure"
		: cause == "the output I/O failure" ? "output I/O failure"
						    : cause;

	EXPECT_NE(c->error.data.find(expected), std::string::npos)
		<< c->error.data;
	if (c->failure == "a closed pipe consumer") {
		CliContext probe;

		probe.file("event.xml", c->expected);
		run(probe, "cot encode", "closed_pipe");
		EXPECT_EQ(probe.status, 1);
		EXPECT_NE(probe.error.data.find("output I/O failure"),
			std::string::npos);
	}
}

THEN("^stdout is empty$")
{
	ScenarioScope<CliContext> c;

	EXPECT_TRUE(c->output.data.empty());
}

THEN("^\"([^\"]*)\" still contains exactly \"([^\"]*)\"$")
{
	REGEX_PARAM(std::string, path);
	REGEX_PARAM(std::string, data);
	ScenarioScope<CliContext> c;

	EXPECT_EQ(c->read(path), data);
}

GIVEN("^\"event.pb\" contains a valid XML-representable unframed Protobuf "
	"event$")
{
	ScenarioScope<CliContext> c;
	owned_event owner;

	c->expected = xml();
	ASSERT_EQ(decode(COT_FORMAT_XML, c->expected, owner), COT_STATUS_OK);
	c->file("event.pb", encoded_event(COT_FORMAT_PROTOBUF, *owner.get()));
}

WHEN("^I run the shell pipeline \"cot decode < event.pb \\| cot encode > "
	"roundtrip.pb\"$")
{
	ScenarioScope<CliContext> c;

	run(*c, "cot", "pipeline");
}

THEN("^both commands exit 0$")
{
	ScenarioScope<CliContext> c;

	EXPECT_EQ(c->status, 0);
}

THEN("^\"roundtrip.pb\" decodes to the same represented fields as "
	"\"event.pb\"$")
{
	ScenarioScope<CliContext> c;
	owned_event actual;
	owned_event expected;

	ASSERT_EQ(decode(COT_FORMAT_PROTOBUF, c->read("roundtrip.pb"), actual),
		COT_STATUS_OK);
	ASSERT_EQ(decode(COT_FORMAT_PROTOBUF, c->read("event.pb"), expected),
		COT_STATUS_OK);
	same_event(actual.get(), expected.get());
}

THEN("^both commands have empty stderr$")
{
	ScenarioScope<CliContext> c;

	EXPECT_TRUE(c->error.data.empty());
}

GIVEN("^\"([^\"]*)\" contains (malformed XML|truncated Protobuf|a control-only "
	"TakMessage|an event-plus-control TakMessage|an event with an opaque "
	"extension|an event with an XML-inexpressible timestamp)$")
{
	REGEX_PARAM(std::string, path);
	REGEX_PARAM(std::string, name);
	ScenarioScope<CliContext> c;

	c->file(path, fixture(name));
}

GIVEN("^\"([^\"]*)\" does not exist$")
{
	REGEX_PARAM(std::string, path);
	ScenarioScope<CliContext> c;

	/* Same wording is also used to assert noncreation after the command. */
	EXPECT_FALSE(fs::exists(c->directory / path));
	EXPECT_EQ(c->files.count(path), 0U);
}

GIVEN("^stdin contains a valid XML event$")
{
	ScenarioScope<CliContext> c;

	c->input.data = c->expected = xml();
}

GIVEN("^the test environment injects a codec allocation failure$")
{
	ScenarioScope<CliContext> c;

	c->inject = true;
}

GIVEN("^a valid XML event whose Protobuf encoding contains NUL, LF, and CR "
	"bytes$")
{
	ScenarioScope<CliContext> c;

	c->input.data = c->expected = xml();
	replace(c->expected, "uid='alpha-1'", "uid='alpha&#10;&#13;'");
	c->input.data = c->expected;
	owned_event owner;

	ASSERT_EQ(decode(COT_FORMAT_XML, c->expected, owner), COT_STATUS_OK);
	c->source = encoded_event(COT_FORMAT_PROTOBUF, *owner.get());
	for (char byte : {'\0', '\n', '\r'})
		ASSERT_NE(c->source.find(byte), std::string::npos);
}

WHEN("^I run \"cot encode\" on Windows with that XML on stdin$")
{
	ScenarioScope<CliContext> c;

	run(*c, "cot encode", "windows");
}

THEN("^stdout equals the public API's counted Protobuf result byte for byte$")
{
	ScenarioScope<CliContext> c;

	EXPECT_EQ(c->output.data, c->source);
}

THEN("^no newline or terminator is appended$")
{
	ScenarioScope<CliContext> c;

	EXPECT_EQ(c->output.data.size(), c->source.size());
}

GIVEN("^a valid Protobuf event containing NUL, LF, CR, and byte 1a$")
{
	ScenarioScope<CliContext> c;
	cot_event value = event();
	std::string uid = "alpha\n\r";

	/* Unknown binary fields must not terminate input reads. */
	auto unknown = wire_string(99, std::string("\0\n\r\x1a", 4));

	value.uid = text(uid);
	c->source = encoded_event(COT_FORMAT_PROTOBUF, value);
	c->source.append(unknown.begin(), unknown.end());
	c->input.data = c->source;
}

WHEN("^I run \"cot decode\" on Windows with those bytes on stdin$")
{
	ScenarioScope<CliContext> c;

	run(*c, "cot decode", "windows");
}

THEN("^stdout represents the complete event without early EOF or byte "
	"translation$")
{
	ScenarioScope<CliContext> c;
	owned_event expected;
	owned_event actual;

	ASSERT_EQ(decode(COT_FORMAT_PROTOBUF, c->source, expected),
		COT_STATUS_OK);
	ASSERT_EQ(
		decode(COT_FORMAT_XML, c->output.data, actual), COT_STATUS_OK);
	same_event(actual.get(), expected.get());
}

GIVEN("^(input.xml does not exist|reading input.xml fails before EOF|closing "
	"input.xml fails after EOF)$")
{
	REGEX_PARAM(std::string, failure);
	ScenarioScope<CliContext> c;

	c->failure = failure;
	if (failure != "input.xml does not exist")
		c->file("input.xml", xml());
}

GIVEN("^the selected output encounters (.*)$")
{
	REGEX_PARAM(std::string, failure);
	ScenarioScope<CliContext> c;

	c->failure = failure;
}

WHEN("^I run \"cot encode event.xml\" with that output selected$")
{
	ScenarioScope<CliContext> c;

	run(*c, c->failure == "a closed pipe consumer"
			? "cot encode event.xml"
			: "cot encode event.xml -o output.pb");
}

THEN("^any written output is only a prefix of the complete result$")
{
	ScenarioScope<CliContext> c;
	owned_event expected;

	ASSERT_EQ(decode(COT_FORMAT_XML, c->expected, expected), COT_STATUS_OK);
	std::string complete =
		encoded_event(COT_FORMAT_PROTOBUF, *expected.get());
	std::string actual = c->files.count("output.pb")
				     ? c->files.at("output.pb").data
				     : c->output.data;

	EXPECT_LE(actual.size(), complete.size());
	EXPECT_EQ(actual, complete.substr(0, actual.size()));
}

GIVEN("^the test output accepts only a few bytes on each write$")
{
	ScenarioScope<CliContext> c;

	c->short_writes = true;
}

THEN("^stdout equals the complete public API result byte for byte$")
{
	ScenarioScope<CliContext> c;
	owned_event expected;

	ASSERT_EQ(decode(COT_FORMAT_XML, c->expected, expected), COT_STATUS_OK);
	EXPECT_EQ(c->output.data,
		encoded_event(COT_FORMAT_PROTOBUF, *expected.get()));
}

GIVEN("^a valid conversion and an output that fails during writing$")
{
	ScenarioScope<CliContext> c;

	c->file("event.xml", xml());
	c->failure = "write failure";
}

WHEN("^I run the corresponding cot command$")
{
	ScenarioScope<CliContext> c;

	run(*c, "cot encode event.xml -o output.pb");
}

THEN("^all owned files, input storage, decoded owners, and buffers are "
	"released$")
{
	ScenarioScope<CliContext> c;

	EXPECT_EQ(c->opens, 2U);
	EXPECT_EQ(c->opens, c->closes);
	EXPECT_EQ(live_allocations(), c->allocation_baseline);
	for (const auto &file : c->files)
		EXPECT_TRUE(file.second.closed);
}

struct AtomicContext {
	CliContext files;
	void *token = nullptr;
	FILE *stream = nullptr;
	fs::path temporary;

	~AtomicContext()
	{
		if (stream)
			fclose(stream);
		if (token)
			cot_cli_discard(nullptr, token);
	}
};

GIVEN("^an existing regular output with mode 0644$")
{
	ScenarioScope<AtomicContext> c;

	c->files.file("output.pb", "keep me");
	fs::permissions(c->files.directory / "output.pb",
		fs::perms::owner_read | fs::perms::owner_write |
		fs::perms::group_read | fs::perms::others_read);
}

static void prepare_atomic(AtomicContext &c)
{
	std::string path = (c.files.directory / "output.pb").string();

	c.stream = static_cast<FILE *>(
		cot_cli_temporary(nullptr, path.c_str(), &c.token));
	ASSERT_NE(c.stream, nullptr);
	for (const auto &entry : fs::directory_iterator(c.files.directory))
		if (entry.path().filename().string().find(".cot-") == 0)
			c.temporary = entry.path();
	ASSERT_FALSE(c.temporary.empty());
}

WHEN("^I prepare a named output transaction$")
{
	ScenarioScope<AtomicContext> c;

	prepare_atomic(*c);
}

THEN("^its temporary file has mode 0600$")
{
	ScenarioScope<AtomicContext> c;

	EXPECT_EQ(fs::status(c->temporary).permissions() & fs::perms::mask,
		fs::perms::owner_read | fs::perms::owner_write);
}

THEN("^the destination is still unchanged$")
{
	ScenarioScope<AtomicContext> c;

	EXPECT_EQ(c->files.read("output.pb"), "keep me");
}

WHEN("^I prepare and publish a complete named output transaction$")
{
	ScenarioScope<AtomicContext> c;

	prepare_atomic(*c);
	ASSERT_EQ(fwrite("complete", 1, 8, c->stream), 8u);
	ASSERT_EQ(fflush(c->stream), 0);
	int closed = fclose(c->stream);

	c->stream = nullptr;
	ASSERT_EQ(closed, 0);
	std::string path = (c->files.directory / "output.pb").string();

	ASSERT_EQ(cot_cli_replace(nullptr, c->token, path.c_str()), 0);
	EXPECT_EQ(c->files.read("output.pb"), "complete");
}

WHEN("^another file is created at the old temporary pathname$")
{
	ScenarioScope<AtomicContext> c;
	std::ofstream stream(c->temporary, std::ios::binary);

	stream << "unrelated file";
}

THEN("^transaction cleanup preserves that other file$")
{
	ScenarioScope<AtomicContext> c;

	int cleanup = cot_cli_discard(nullptr, c->token);

	c->token = nullptr;
	EXPECT_EQ(cleanup, 0);
	EXPECT_TRUE(fs::exists(c->temporary));
}

GIVEN("^\"([^\"]*)\" has mode (0[0-7]{3})$")
{
	REGEX_PARAM(std::string, name);
	REGEX_PARAM(std::string, mode);
	ScenarioScope<CliContext> c;
	fs::path path = c->directory / name;
	auto expected = static_cast<fs::perms>(std::stoul(mode, nullptr, 8));

	if (c->status < 0)
		fs::permissions(path, expected);
	else
		EXPECT_EQ(fs::status(path).permissions() & fs::perms::mask,
			expected);
}

THEN("^no named-output temporary remains$")
{
	ScenarioScope<CliContext> c;

	for (const auto &entry : fs::directory_iterator(c->directory))
		EXPECT_NE(entry.path().filename().string().find(".cot-"), 0u);
}

GIVEN("^the output path is a (symlink|dangling symlink|directory|FIFO)$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<CliContext> c;
	fs::path path = c->directory / "output.pb";

	if (kind == "symlink") {
		c->file("target.pb", "keep me");
		fs::create_symlink("target.pb", path);
	} else if (kind == "dangling symlink") {
		fs::create_symlink("missing.pb", path);
	} else if (kind == "directory") {
		fs::create_directory(path);
	} else {
#ifndef _WIN32
		ASSERT_EQ(mkfifo(path.c_str(), 0600), 0);
#else
		GTEST_SKIP() << "FIFO destination requires POSIX";
#endif
	}
}

THEN("^the (symlink|dangling symlink|directory|FIFO) destination remains "
	"unchanged$")
{
	REGEX_PARAM(std::string, kind);
	ScenarioScope<CliContext> c;
	fs::path path = c->directory / "output.pb";

	if (kind == "symlink" || kind == "dangling symlink") {
		EXPECT_TRUE(fs::is_symlink(fs::symlink_status(path)));
		if (kind == "symlink")
			EXPECT_EQ(c->read("target.pb"), "keep me");
		else
			EXPECT_FALSE(fs::exists(c->directory / "missing.pb"));
	} else if (kind == "directory") {
		EXPECT_TRUE(fs::is_directory(path));
	} else {
		EXPECT_TRUE(fs::is_fifo(path));
	}
}

THEN("^the temporary output was discarded$")
{
	ScenarioScope<CliContext> c;

	EXPECT_EQ(c->mode, "fake");
	EXPECT_EQ(c->files.count(".cot-output.tmp"), 0u);
}

WHEN("^another file replaces the unpublished temporary pathname$")
{
	ScenarioScope<AtomicContext> c;

	ASSERT_TRUE(fs::remove(c->temporary));
	std::ofstream stream(c->temporary, std::ios::binary);

	stream << "unrelated file";
}

THEN("^publication rejects the replacement and cleanup preserves that file$")
{
	ScenarioScope<AtomicContext> c;

	int closed = fclose(c->stream);

	c->stream = nullptr;
	ASSERT_EQ(closed, 0);
	std::string path = (c->files.directory / "output.pb").string();

	EXPECT_NE(cot_cli_replace(nullptr, c->token, path.c_str()), 0);
	int cleanup = cot_cli_discard(nullptr, c->token);

	c->token = nullptr;
	EXPECT_NE(cleanup, 0);
	EXPECT_TRUE(fs::exists(c->temporary));
}

#ifndef _WIN32
struct restrictive_umask {
	mode_t original = umask(0777);

	~restrictive_umask()
	{
		umask(original);
	}
};
#endif

WHEN("^I prepare a named output transaction with umask 0777$")
{
	ScenarioScope<AtomicContext> c;
#ifndef _WIN32
	restrictive_umask mode;
#endif

	prepare_atomic(*c);
}

} // namespace
