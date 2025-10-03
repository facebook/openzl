// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <iostream>
#include <string>

#include "openzl/cpp/poly/StringView.hpp"

#include "tools/sddl/compiler/Grouper.h"
#include "tools/sddl/compiler/Logger.h"
#include "tools/sddl/compiler/Parser.h"
#include "tools/sddl/compiler/Serializer.h"
#include "tools/sddl/compiler/Tokenizer.h"

namespace openzl::sddl {

class Compiler {
   public:
    struct Options;

    /**
     * Creates a compiler instance with the given @p options;
     */
    explicit Compiler(Options options = Options{});

    /**
     * This function translates a program @p source in the Data Description
     * Driven Dispatch language to the binary compiled representation that the
     * SDDL graph accepts in OpenZL.
     *
     * @param source a human-readable description in the SDDL Language.
     * @param filename an optional string identifying the source of the @p
     *                 source code, which will be included in the pretty
     *                 error message if compilation fails. If the input
     *                 didn't come from a source readily identifiable with a
     *                 string that would be meaningful to the user / consumer
     *                 of error messages, you can just use `[input]` or some-
     *                 thing, I dunno.
     * @returns the compiled binary representation of the description, which
     *          the SDDL graph accepts. See the SDDL graph documentation for
     *          a description of the format of this representation.
     * @throws CompilerException if compilation fails. Additional context can
     *         be found in the output log provided to the compiler during
     *         construction, if a suitably high verbosity has been selected.
     */
    std::string compile(poly::string_view source, poly::string_view filename)
            const;

    /**
     * Argument pack for the SDDL compiler. It offers convenient builder
     * methods so you can choose which options to set and leave the others
     * defaulted, as in e.g.:
     *
     * ```
     * std::stringstream compiler_logs;
     * const Compiler compiler{
     *   Options{}.with_log(compiler_logs)
     *            .with_more_verbose()
     *            .with_more_verbose()
     *            .with_debug_info()
     * };
     * ```
     */
    struct Options {
        explicit Options();

        /**
         * Set a different ostream for logs. (Takes a non-owning reference. The
         * given stream must outlive the compiler.)
         */
        Options& with_log(std::ostream& os) &;
        Options&& with_log(std::ostream& os) &&;

        /**
         * Set an explicit verbosity level for logs.
         *
         * Currently, negative levels produce no output, 0 logs errors, and
         * positive levels log increasing amounts of internal / debug state
         * logs.
         */
        Options& with_verbosity(int v) &;
        Options&& with_verbosity(int v) &&;

        /**
         * Increment the verbosity.
         */
        Options& with_more_verbose() &;
        Options&& with_more_verbose() &&;

        /**
         * Decrement the verbosity.
         */
        Options& with_less_verbose() &;
        Options&& with_less_verbose() &&;

        /**
         * Whether to include debug info in the compiled output. This
         * information is not necessary for correct execution, but it helps the
         * execution engine produce useful error messages when execution
         * fails.
         */
        Options& with_debug_info(bool d = true) &;
        Options&& with_debug_info(bool d = true) &&;

        /**
         * Don't include debug info in the compiled output.
         */
        Options& with_no_debug_info() &;
        Options&& with_no_debug_info() &&;

        std::ostream* log_os{ &std::cerr };
        int verbosity{ 0 };
        bool include_debug_info{ true };
    };

   private:
    const Options options_;

    const detail::Logger logger_;

    const Tokenizer tokenizer_;
    const Grouper grouper_;
    const Parser parser_;
    const Serializer serializer_;
};

} // namespace openzl::sddl
