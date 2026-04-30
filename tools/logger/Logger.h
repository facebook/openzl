// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdio>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace openzl::tools::logger {

// Global verbosity functions
enum LogLevel {
    ALWAYS     = 0,
    ERRORS     = 1,
    WARNINGS   = 2,
    INFO       = 3,
    VERBOSE1   = 4,
    VERBOSE2   = 5,
    VERBOSE3   = 6,
    EVERYTHING = 7,
};

const int progressBarWidth = 50;

/**
 * Logger class for CLI and training
 */
class Logger {
   public:
    static Logger& instance()
    {
        static Logger instance_;
        return instance_;
    }

    int global_verbosity;      // TODO where to set this by default?
    bool progress_line_active; // Track if we have an active progress line

    // Store current progress information for re-printing
    LogLevel progress_level;
    double progress_value;
    std::string progress_message;

    void setGlobalLoggerVerbosity(int verbosity)
    {
        if (verbosity < static_cast<int>(ALWAYS)
            || verbosity > static_cast<int>(EVERYTHING)) {
            throw std::invalid_argument(
                    "Invalid log level: " + std::to_string(verbosity)
                    + ". Valid levels are "
                    + std::to_string(static_cast<int>(ALWAYS)) + " (ALWAYS) to "
                    + std::to_string(static_cast<int>(EVERYTHING))
                    + " (EVERYTHING).");
        }
        global_verbosity = verbosity;
    }
    int getGlobalLoggerVerbosity()
    {
        return global_verbosity;
    }

   private:
    Logger()
            : global_verbosity(INFO),
              progress_line_active(false),
              progress_level(INFO),
              progress_value(0.0),
              progress_message("")
    {
    }
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    template <typename Arg>
    static void log_inner(std::ostream& os, const Arg& arg)
    {
        os << arg;
    }
    template <typename Arg, typename... Args>
    static void log_inner(std::ostream& os, const Arg& arg, const Args&... args)
    {
        log_inner(os << arg, args...);
    }

   public:
    template <typename... Args>
    static void log(LogLevel level, const Args&... args)
    {
        if (shouldLog(level)) {
            finalizeProgressIfActive();
            log_inner(std::cerr, args...);
            std::cerr << '\n';
            reprintProgressIfActive();
        }
    }

   public:
    template <typename... Args>
    static void log_c(LogLevel level, const char* format, const Args&... args)
    {
        if (!shouldLog(level)) {
            return;
        }

        finalizeProgressIfActive();

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        fprintf(stderr, format, args...);
        fprintf(stderr, "\n");
#pragma GCC diagnostic pop

        reprintProgressIfActive();
    }

   public:
    template <typename... Args>
    static void update(LogLevel level, const char* format, const Args&... args)
    {
        if (!shouldLog(level)) {
            return;
        }

        // Move to beginning of line
        // TODO remove control characters when printing to non-tty
        fprintf(stderr, "\r");

        // Print the formatted message
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        fprintf(stderr, format, args...);
#pragma GCC diagnostic pop

        // Clear to end of line to remove any remaining characters
        // TODO remove control characters when printing to non-tty
        fprintf(stderr, "%s", CLEAR_TO_EOL);

        fflush(stderr);
    }

    template <typename... Args>
    static void logProgress(
            LogLevel level,
            double progress,
            const char* format,
            Args&&... args)
    {
        if (!shouldLog(level)) {
            return;
        }

        if (progress > 1.0) {
            throw std::invalid_argument(
                    "Progress percentage must be <= 1.0, got: "
                    + std::to_string(progress) + ".");
        }

        instance().progress_line_active = true;

        // Store current progress information for re-printing
        instance().progress_level = level;
        instance().progress_value = progress;

        // Build the user message part
        std::string userMsg;
        if (format && format[0] != '\0') {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
            int required_size = snprintf(nullptr, 0, format, args...);
            if (required_size > 0) {
                std::vector<char> buffer(
                        required_size + 1); // +1 for null terminator
                snprintf(buffer.data(), buffer.size(), format, args...);
                userMsg = std::string(buffer.data());
            }
#pragma GCC diagnostic pop
        }

        // Build the progress message
        int filled = (int)(progress * progressBarWidth);
        char progressBar[progressBarWidth + 3]; // progressBarWidth + 2 for ends
                                                // + 1 for null terminator
        progressBar[0] = '[';
        for (int i = 0; i < progressBarWidth; ++i) {
            progressBar[i + 1] = (i < filled) ? '=' : '-';
        }
        progressBar[progressBarWidth + 1] = ']';
        progressBar[progressBarWidth + 2] = '\0';
        instance().progress_message = std::string(progressBar) + " " + userMsg;

        update(level, "%s", instance().progress_message.c_str());
    }

    // Finalize an update line by adding a newline
   public:
    static void finalizeUpdate(LogLevel level)
    {
        if (!shouldLog(level)) {
            return;
        }

        // Add newline
        fprintf(stderr, "\n");
    }

    // Finalize an UPDATE line by adding a newline
    static void finalizeProgress(LogLevel level)
    {
        finalizeUpdate(level);
        instance().progress_line_active = false;
    }

   private:
    // ANSI terminal control sequences
    static constexpr const char* CLEAR_TO_EOL = "\033[K";

    static constexpr int PADDING_SIZE = 80;

    static bool shouldLog(LogLevel level);
    static void clearLine();
    static void finalizeProgressIfActive();
    static void reprintProgressIfActive();
};

} // namespace openzl::tools::logger
