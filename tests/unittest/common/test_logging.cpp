// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "openzl/common/logging.h"
#include "openzl/zl_config.h"
#include "openzl/zl_logging.h"

namespace {

struct LogRecord {
    std::string file;
    int line;
    std::string message;
};

void captureLog(
        void* opaque,
        const char* file,
        int line,
        const char* prefix,
        const char* format,
        va_list args) noexcept
{
    char message[128];
#if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    std::vsnprintf(message, sizeof(message), format, args);
#if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic pop
#endif

    auto& records = *static_cast<std::vector<LogRecord>*>(opaque);
    records.push_back(
            { file == nullptr ? "" : file,
              line,
              std::string(prefix == nullptr ? "" : prefix) + message });
}

TEST(LoggingTest, customHandlerReceivesLogs)
{
    std::vector<LogRecord> records;
    ZL_setLogHandler(captureLog, &records);

    ZL_LOG_func("source.c", "function", 17, "value %d", 42);
    ZL_LOG_func_if_nonempty(
            "source.c", "function", 23, "prefix: ", "%s", "detail");
    ZL_RLOG_func("raw %d", 7);

    ZL_resetLogHandler();

    ASSERT_EQ(records.size(), 3);
    EXPECT_EQ(records[0].file, "source.c");
    EXPECT_EQ(records[0].line, 17);
    EXPECT_EQ(records[0].message, "value 42");
    EXPECT_EQ(records[1].file, "source.c");
    EXPECT_EQ(records[1].line, 23);
    EXPECT_EQ(records[1].message, "prefix: detail");
    EXPECT_EQ(records[2].file, "");
    EXPECT_EQ(records[2].line, 0);
    EXPECT_EQ(records[2].message, "raw 7");
}

TEST(LoggingTest, nullHandlerDisablesLogs)
{
    std::vector<LogRecord> records;
    ZL_setLogHandler(nullptr, &records);
    ZL_RLOG_func("not captured");
    ZL_resetLogHandler();
    EXPECT_TRUE(records.empty());
}

TEST(LoggingTest, defaultHandlerPreservesStderrFormat)
{
#if ZL_ENABLE_STDERR_LOGGING
    ZL_resetLogHandler();
    testing::internal::CaptureStderr();
    ZL_LOG_func("source.c", "function", 17, "value %d", 42);
    ZL_RLOG_func("raw %d", 7);
    EXPECT_EQ(
            testing::internal::GetCapturedStderr(),
            "source.c:17: value 42\nraw 7");
#else
    GTEST_SKIP() << "stderr logging is disabled in this build";
#endif
}

} // namespace
