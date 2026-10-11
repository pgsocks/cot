#include <gtest/gtest.h>
#include "support.h"

#include <array>
#include <cstdlib>
#include <string>

namespace {

const char *const debug_variables[] = {
	"EXPAT_ENTROPY_DEBUG", "EXPAT_MALLOC_DEBUG",
	"EXPAT_ACCOUNTING_DEBUG", "EXPAT_ENTITY_DEBUG"
};

void set_variable(const char *name, const char *value)
{
#ifdef _WIN32
	EXPECT_EQ(_putenv_s(name, value ? value : ""), 0);
#else
	EXPECT_EQ(value ? setenv(name, value, 1) : unsetenv(name), 0);
#endif
}

class parser_debug_environment {
public:
	parser_debug_environment()
	{
		for (size_t i = 0; i < saved.size(); ++i) {
			const char *value = std::getenv(debug_variables[i]);

			present[i] = value != nullptr;
			if (value)
				saved[i] = value;
			set_variable(debug_variables[i], "1");
		}
	}

	~parser_debug_environment()
	{
		for (size_t i = 0; i < saved.size(); ++i)
			set_variable(debug_variables[i],
				present[i] ? saved[i].c_str() : nullptr);
	}

private:
	std::array<std::string, 4> saved;
	std::array<bool, 4> present{};
};

TEST(ParserIsolation, DebugEnvironmentCannotEnableCodecIo)
{
	parser_debug_environment environment;
	cot_test::owned_event owner;

	testing::internal::CaptureStdout();
	testing::internal::CaptureStderr();
	cot_status status = cot_test::decode(COT_FORMAT_XML,
		cot_test::xml("<custom>A &amp; B</custom>"), owner);
	std::string errors = testing::internal::GetCapturedStderr();
	std::string output = testing::internal::GetCapturedStdout();

	EXPECT_EQ(status, COT_STATUS_OK);
	EXPECT_TRUE(errors.empty());
	EXPECT_TRUE(output.empty());
}

}
