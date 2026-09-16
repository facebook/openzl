// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "tools/logger/Logger.h"

using namespace openzl::tools::logger;

namespace {

constexpr const char* kClearToEol = "\033[K";

// Progress expressed in bar cells, so that these tests do not depend on the
// value of progressBarWidth. Starting from 0.5 -- an exact cell boundary --
// half a cell forward cannot change the bar, and one and a half cells forward
// must advance it by exactly one.
constexpr double kHalfCell = 0.5 / progressBarWidth;

struct CapturedProgress {
    double progress;
    std::string message;
};

std::vector<CapturedProgress>*& progressEventSink()
{
    static thread_local std::vector<CapturedProgress>* events{};
    return events;
}

void captureProgress(double progress, const std::string& message)
{
    progressEventSink()->push_back({ progress, message });
}

class LoggerTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        setIsTTY(false);
        Logger::setProgressCallback(nullptr);
        progressEventSink() = &events_;
        ::testing::internal::CaptureStderr();
    }

    void TearDown() override
    {
        // Leave nothing captured behind, even if the test failed early.
        (void)::testing::internal::GetCapturedStderr();
        Logger::setProgressCallback(nullptr);
        progressEventSink() = nullptr;
    }

    void setIsTTY(bool isTTY)
    {
        auto& logger = Logger::instance();
        logger.setGlobalLoggerVerbosity(INFO);
        logger.progress_line_active = false;
        logger.progress_value       = 0.0;
        // Also resets the remembered update message.
        logger.setIsTTY(isTTY);
        isTTY_ = isTTY;
    }

    std::string captured()
    {
        std::string output = ::testing::internal::GetCapturedStderr();
        ::testing::internal::CaptureStderr();
        return output;
    }

    static size_t countBarCells(const std::string& output)
    {
        return (size_t)std::count(output.begin(), output.end(), '=');
    }

    // The bytes update() is expected to emit for a message it decides to print.
    std::string updateOutput(const std::string& message) const
    {
        if (isTTY_) {
            return "\r" + message + kClearToEol;
        }
        return message + "\n";
    }

    void checkUpdateWritesTheMessage()
    {
        Logger::update(INFO, "hello %d", 42);

        EXPECT_EQ(captured(), updateOutput("hello 42"));
    }

    void checkRepeatedUpdateIsNotRewritten()
    {
        Logger::update(INFO, "same message");
        EXPECT_EQ(captured(), updateOutput("same message"));

        Logger::update(INFO, "same message");
        EXPECT_EQ(captured(), "");
    }

    void checkRepeatedUpdateComparesTheFormattedMessage()
    {
        Logger::update(INFO, "progress: %d%%", 10);
        EXPECT_EQ(captured(), updateOutput("progress: 10%"));

        // Same format string, different result: must be written.
        Logger::update(INFO, "progress: %d%%", 20);
        EXPECT_EQ(captured(), updateOutput("progress: 20%"));

        // Different format string, same result: must not be written.
        Logger::update(INFO, "%s", "progress: 20%");
        EXPECT_EQ(captured(), "");
    }

    void checkUpdateIsWrittenAgainAfterFinalize()
    {
        Logger::update(INFO, "message");
        EXPECT_EQ(captured(), updateOutput("message"));

        Logger::finalizeUpdate(INFO);
        // A non-TTY update line is already newline terminated.
        EXPECT_EQ(captured(), isTTY_ ? "\n" : "");

        Logger::update(INFO, "message");
        EXPECT_EQ(captured(), updateOutput("message"));
    }

   private:
    bool isTTY_{ false };

   protected:
    std::vector<CapturedProgress> events_;
};

TEST_F(LoggerTest, ProgressCallbackReceivesUnthrottledProgress)
{
    Logger::setProgressCallback(captureProgress);
    Logger::logProgress(INFO, 0.5, "step %d", 1);
    (void)captured();
    Logger::logProgress(INFO, 0.5 + kHalfCell, "step %d", 2);

    ASSERT_EQ(events_.size(), 2u);
    EXPECT_DOUBLE_EQ(events_[1].progress, 0.5 + kHalfCell);
    EXPECT_EQ(events_[1].message, "step 2");
    EXPECT_EQ(captured(), "");
}

TEST_F(LoggerTest, UpdateWritesTheMessageOnTTY)
{
    setIsTTY(true);
    checkUpdateWritesTheMessage();
}

TEST_F(LoggerTest, UpdateWritesTheMessageOnNonTTY)
{
    setIsTTY(false);
    checkUpdateWritesTheMessage();
}

TEST_F(LoggerTest, RepeatedUpdateIsNotRewrittenOnTTY)
{
    setIsTTY(true);
    checkRepeatedUpdateIsNotRewritten();
}

TEST_F(LoggerTest, RepeatedUpdateIsNotRewrittenOnNonTTY)
{
    setIsTTY(false);
    checkRepeatedUpdateIsNotRewritten();
}

TEST_F(LoggerTest, RepeatedUpdateComparesTheFormattedMessageOnTTY)
{
    setIsTTY(true);
    checkRepeatedUpdateComparesTheFormattedMessage();
}

TEST_F(LoggerTest, RepeatedUpdateComparesTheFormattedMessageOnNonTTY)
{
    setIsTTY(false);
    checkRepeatedUpdateComparesTheFormattedMessage();
}

TEST_F(LoggerTest, UpdateIsWrittenAgainAfterFinalizeOnTTY)
{
    setIsTTY(true);
    checkUpdateIsWrittenAgainAfterFinalize();
}

TEST_F(LoggerTest, UpdateIsWrittenAgainAfterFinalizeOnNonTTY)
{
    setIsTTY(false);
    checkUpdateIsWrittenAgainAfterFinalize();
}

TEST_F(LoggerTest, LogCWritesTheFormattedLine)
{
    Logger::log_c(INFO, "value %d and %s", 7, "text");

    EXPECT_EQ(captured(), "value 7 and text\n");
}

TEST_F(LoggerTest, LogCWithEmptyFormatWritesOnlyANewline)
{
    Logger::log_c(INFO, "");

    EXPECT_EQ(captured(), "\n");
}

TEST_F(LoggerTest, LogCBelowVerbosityWritesNothing)
{
    Logger::instance().setGlobalLoggerVerbosity(WARNINGS);

    Logger::log_c(INFO, "invisible");

    EXPECT_EQ(captured(), "");
}

TEST_F(LoggerTest, LogCRedrawsTheProgressLineOnTTY)
{
    setIsTTY(true);

    Logger::logProgress(INFO, 0.5, "working");
    const std::string progressMessage = Logger::instance().progress_message;
    ASSERT_EQ(captured(), updateOutput(progressMessage));

    Logger::log_c(INFO, "a %s line", "log");
    const std::string output = captured();

    EXPECT_NE(output.find("a log line\n"), std::string::npos);
    EXPECT_NE(output.find(updateOutput(progressMessage)), std::string::npos);

    Logger::finalizeProgress(INFO);
}

TEST_F(LoggerTest, UpdateBelowVerbosityWritesNothing)
{
    Logger::instance().setGlobalLoggerVerbosity(WARNINGS);

    Logger::update(INFO, "invisible");

    EXPECT_EQ(captured(), "");
}

// A log line wipes out the progress line on a TTY, so the progress line has to
// be re-drawn even though its message is unchanged.
TEST_F(LoggerTest, ProgressIsReprintedAfterLogClearsTheLineOnTTY)
{
    setIsTTY(true);

    Logger::logProgress(INFO, 0.5, "working");
    const std::string progressMessage = Logger::instance().progress_message;
    ASSERT_EQ(captured(), updateOutput(progressMessage));

    Logger::log(INFO, "a log line");
    const std::string output = captured();

    EXPECT_NE(output.find("a log line\n"), std::string::npos);
    EXPECT_NE(output.find(updateOutput(progressMessage)), std::string::npos);

    Logger::finalizeProgress(INFO);
}

// On a non-TTY the progress line was never wiped out, so re-printing it would
// only duplicate it.
TEST_F(LoggerTest, ProgressIsNotReprintedAfterLogOnNonTTY)
{
    setIsTTY(false);

    Logger::logProgress(INFO, 0.5, "working");
    const std::string progressMessage = Logger::instance().progress_message;
    ASSERT_EQ(captured(), updateOutput(progressMessage));

    Logger::log(INFO, "a log line");

    EXPECT_EQ(captured(), "a log line\n");

    Logger::finalizeProgress(INFO);
}

// Same as above, but with the remembered update message cleared in between, so
// that the suppression cannot come from update()'s deduplication.
TEST_F(LoggerTest, ProgressIsNotReprintedAfterLogOnNonTTYWhenNotDeduplicated)
{
    setIsTTY(false);

    Logger::logProgress(INFO, 0.5, "working");
    ASSERT_EQ(captured(), updateOutput(Logger::instance().progress_message));

    // Forgets the previous message, but leaves the progress line active.
    Logger::finalizeUpdate(INFO);
    ASSERT_EQ(captured(), "");
    ASSERT_TRUE(Logger::instance().progress_line_active);

    Logger::log(INFO, "a log line");

    EXPECT_EQ(captured(), "a log line\n");

    Logger::finalizeProgress(INFO);
}

// The message differs on every call, so the only thing that can suppress the
// middle write is the bar itself being unchanged -- which also means a message
// change that does not move the bar is dropped along with it.
TEST_F(LoggerTest, LogProgressSkipsRedrawWhileTheBarIsUnchangedOnNonTTY)
{
    setIsTTY(false);

    Logger::logProgress(INFO, 0.5, "step %d", 1);
    const std::string atBoundary = captured();
    ASSERT_NE(atBoundary.find("step 1"), std::string::npos);

    // Still inside the same cell, where a redraw would cost a whole new line.
    Logger::logProgress(INFO, 0.5 + kHalfCell, "step %d", 2);
    EXPECT_EQ(captured(), "");

    // One cell further along: the bar gains an '=' and must be redrawn.
    Logger::logProgress(INFO, 0.5 + 3 * kHalfCell, "step %d", 3);
    const std::string nextCell = captured();
    EXPECT_NE(nextCell.find("step 3"), std::string::npos);
    EXPECT_EQ(countBarCells(nextCell), countBarCells(atBoundary) + 1);

    Logger::finalizeProgress(INFO);
}

// A TTY redraws the line in place, so a new message is shown immediately even
// though the bar has not moved.
TEST_F(LoggerTest, LogProgressRedrawsWithinTheSameBarCellOnTTY)
{
    setIsTTY(true);

    Logger::logProgress(INFO, 0.5, "step %d", 1);
    const std::string atBoundary = captured();
    ASSERT_NE(atBoundary.find("step 1"), std::string::npos);

    Logger::logProgress(INFO, 0.5 + kHalfCell, "step %d", 2);
    const std::string sameCell = captured();
    EXPECT_NE(sameCell.find("step 2"), std::string::npos);
    EXPECT_EQ(countBarCells(sameCell), countBarCells(atBoundary));

    // An identical repeat is still suppressed, by update()'s deduplication.
    Logger::logProgress(INFO, 0.5 + kHalfCell, "step %d", 2);
    EXPECT_EQ(captured(), "");

    Logger::finalizeProgress(INFO);
}

// The skip only applies while a progress line is active, so a new one starting
// at the same value still draws.
TEST_F(LoggerTest, LogProgressDrawsTheSameBarAgainAfterFinalize)
{
    setIsTTY(false);

    Logger::logProgress(INFO, 0.5, "working");
    const std::string first = captured();
    ASSERT_FALSE(first.empty());

    Logger::finalizeProgress(INFO);
    ASSERT_EQ(captured(), "");

    Logger::logProgress(INFO, 0.5, "working");

    EXPECT_EQ(captured(), first);
}

// A finished progress line leaves no stale progress behind.
TEST_F(LoggerTest, FinalizeProgressResetsTheProgressValue)
{
    Logger::logProgress(INFO, 0.5, "working");
    ASSERT_DOUBLE_EQ(Logger::instance().progress_value, 0.5);
    ASSERT_TRUE(Logger::instance().progress_line_active);

    Logger::finalizeProgress(INFO);

    EXPECT_DOUBLE_EQ(Logger::instance().progress_value, 0.0);
    EXPECT_FALSE(Logger::instance().progress_line_active);
}

// The reset is unconditional, so a progress line that was never drawn because
// of the verbosity level does not leave a value behind either.
TEST_F(LoggerTest, FinalizeProgressResetsTheProgressValueBelowVerbosity)
{
    Logger::logProgress(INFO, 0.5, "working");
    ASSERT_DOUBLE_EQ(Logger::instance().progress_value, 0.5);

    Logger::instance().setGlobalLoggerVerbosity(WARNINGS);
    Logger::finalizeProgress(INFO);

    EXPECT_DOUBLE_EQ(Logger::instance().progress_value, 0.0);
    EXPECT_FALSE(Logger::instance().progress_line_active);
}

// Non-TTY output must not contain carriage returns or ANSI escapes, including
// the space padding used to clear the progress line.
TEST_F(LoggerTest, NonTTYOutputHasNoTerminalControlSequences)
{
    setIsTTY(false);

    Logger::logProgress(INFO, 0.25, "step %d", 1);
    Logger::log(INFO, "a log line");
    Logger::logProgress(INFO, 0.75, "step %d", 2);
    Logger::finalizeProgress(INFO);
    const std::string output = captured();

    EXPECT_EQ(output.find('\r'), std::string::npos);
    EXPECT_EQ(output.find('\033'), std::string::npos);
    EXPECT_EQ(output.find("  "), std::string::npos);
}

} // namespace
