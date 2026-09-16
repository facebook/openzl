// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdio>
#include <iostream>
#include <optional>
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
    // Allows callers to receive progress updates in addition to normal CLI
    // output.
    using ProgressCallback =
            void (*)(double progress, const std::string& message);

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

    // Progress reporting is not thread-safe. Callers must serialize calls to
    // logProgress().
    static void setProgressCallback(ProgressCallback callback);

    /**
     * Overrides the auto-detected TTY status of stderr. Exposed for tests, so
     * that they can exercise both the TTY and the non-TTY code paths.
     */
    void setIsTTY(bool isTTY)
    {
        is_tty = isTTY;
        previous_update_message.reset();
    }
    bool isTTY() const
    {
        return is_tty;
    }

   private:
    Logger()
            : global_verbosity(INFO),
              progress_line_active(false),
              progress_level(INFO),
              progress_value(0.0),
              progress_message(""),
              is_tty(stderrIsTTY())
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

    template <typename... Args>
    static std::string formatToString(const char* format, const Args&... args)
    {
        if (format == nullptr || format[0] == '\0') {
            return {};
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
        const int required_size = snprintf(nullptr, 0, format, args...);
        if (required_size <= 0) {
            return {};
        }
        std::vector<char> buffer(required_size + 1); // +1 for null terminator
        snprintf(buffer.data(), buffer.size(), format, args...);
#pragma GCC diagnostic pop
        return std::string(buffer.data(), (size_t)required_size);
    }

    // Number of '=' cells the bar shows for this progress value.
    static constexpr int progressBarFilled(double progress)
    {
        return (int)(progress * progressBarWidth);
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

        fprintf(stderr, "%s\n", formatToString(format, args...).c_str());

        reprintProgressIfActive();
    }

   public:
    template <typename... Args>
    static void update(LogLevel level, const char* format, const Args&... args)
    {
        if (!shouldLog(level)) {
            return;
        }

        updateLine(formatToString(format, args...));
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

        // On a non-TTY every redraw costs a whole new line, so wait until the
        // bar gains or loses an '=' before spending one. A TTY redraws the
        // line in place, so there it is not worth withholding an update.
        const ProgressCallback callback = instance().progress_callback;
        const int filled                = progressBarFilled(progress);
        const bool skipRedraw           = !instance().is_tty
                && instance().progress_line_active
                && filled == progressBarFilled(instance().progress_value);
        if (skipRedraw && !callback) {
            return;
        }

        // Build the user message part
        const std::string userMsg = formatToString(format, args...);

        if (callback) {
            callback(progress, userMsg);
        }
        if (skipRedraw) {
            return;
        }

        instance().progress_line_active = true;

        // Store current progress information for re-printing
        instance().progress_level = level;
        instance().progress_value = progress;

        // Build the progress message
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

        finalizeUpdateLine();
    }

    // Finalize an UPDATE line by adding a newline
    static void finalizeProgress(LogLevel level)
    {
        finalizeUpdate(level);
        instance().progress_line_active = false;
        instance().progress_value       = 0.0;
    }

   private:
    // ANSI terminal control sequences
    static constexpr const char* CLEAR_TO_EOL = "\033[K";

    static constexpr int PADDING_SIZE = 80;

    // The last message written by update(), or nullopt if the update line has
    // since been cleared or finalized. Repeating a message is a no-op, which
    // keeps non-TTY output free of duplicated progress lines.
    std::optional<std::string> previous_update_message;
    bool is_tty;
    ProgressCallback progress_callback{ nullptr };

    static bool stderrIsTTY();

    static bool shouldLog(LogLevel level);
    static void updateLine(const std::string& message);
    static void finalizeUpdateLine();
    static void clearLine();
    static void finalizeProgressIfActive();
    static void reprintProgressIfActive();
};

} // namespace openzl::tools::logger
