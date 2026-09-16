// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "Logger.h"
#include <cstdlib>

#if defined(_WIN32)
#    include <io.h> // @manual
#else
#    include <unistd.h>
#endif

namespace openzl::tools::logger {

void Logger::setProgressCallback(ProgressCallback callback)
{
    instance().progress_callback = callback;
}

bool Logger::stderrIsTTY()
{
#if defined(_WIN32)
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

bool Logger::shouldLog(LogLevel level)
{
    return static_cast<int>(level) <= instance().global_verbosity;
}

void Logger::updateLine(const std::string& message)
{
    auto& logger = instance();
    if (logger.previous_update_message == message) {
        return;
    }
    logger.previous_update_message = message;

    if (logger.is_tty) {
        // Return to the beginning of the line, overwrite it, then clear
        // whatever the previous message left behind.
        fprintf(stderr, "\r%s%s", message.c_str(), CLEAR_TO_EOL);
    } else {
        // Terminal control sequences are meaningless in a file or a pipe, so
        // emit each distinct message as its own line instead.
        fprintf(stderr, "%s\n", message.c_str());
    }

    fflush(stderr);
}

void Logger::finalizeUpdateLine()
{
    auto& logger = instance();
    if (logger.is_tty) {
        fprintf(stderr, "\n");
    }
    // Non-TTY update lines are already newline terminated.
    logger.previous_update_message.reset();
}

void Logger::clearLine()
{
    auto& logger = instance();
    if (!logger.is_tty) {
        // The line cannot be taken back once written to a file or a pipe.
        return;
    }
    logger.previous_update_message.reset();
    fprintf(stderr, "\r");
    for (int i = 0; i < PADDING_SIZE; ++i) {
        fprintf(stderr, " ");
    }
    fprintf(stderr, "\r");
    fflush(stderr);
}

void Logger::finalizeProgressIfActive()
{
    if (instance().progress_line_active) {
        // Clear the current line
        clearLine();
        // The log line will be printed after this function returns
        // Then we need to re-print the progress line
        // We don't set progress_line_active to false here since we want to
        // maintain the progress state
    }
}

void Logger::reprintProgressIfActive()
{
    auto& logger = instance();
    if (!logger.is_tty) {
        // clearLine() left the progress line alone, so re-printing it would
        // only duplicate it.
        return;
    }
    if (logger.progress_line_active && shouldLog(logger.progress_level)) {
        // Re-print the stored progress message
        update(logger.progress_level, "%s", logger.progress_message.c_str());
    }
}

} // namespace openzl::tools::logger
