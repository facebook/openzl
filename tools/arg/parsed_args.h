// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "tools/arg/flag.h"

namespace openzl::arg {

// forward decl
class ArgParser;
class ParsedArgs {
   public:
    explicit ParsedArgs() {}

    // lookups
    int chosenCmd() const;
    std::optional<std::string> immediate() const;

    bool globalHasFlag(const std::string& name) const;
    std::optional<std::string> globalFlag(const std::string& name) const;
    std::string globalRequiredFlag(const std::string& name) const;

    /**
     * @brief Retrieve all values for a variadic positional argument.
     *
     * Returns a vector containing all values that were passed for the specified
     * positional argument. The number of elements in this vector depends on the
     * number of arguments specified for the positional argument. The size of
     * the vector is pre-validated to match the number of arguments specified.
     *
     * @param cmd The command ID for which to retrieve the positional argument
     * values.
     * @param name The name of the positional argument.
     * @return A vector of strings containing all values for the positional
     * argument.
     */
    std::vector<std::string> cmdPositionals(int cmd, const std::string& name)
            const;

    /**
     * @brief Retrieve the value for a positional argument.
     *
     * Convenience overload that assumes exactly one value (NumArgs::One).
     */
    std::string cmdPositional(int cmd, const std::string& name) const;

    bool cmdHasFlag(int cmd, const std::string& name) const;
    std::optional<std::string> cmdFlag(int cmd, const std::string& name) const;
    std::string cmdRequiredFlag(int cmd, const std::string& name) const;

   private:
    std::map<int, std::map<std::string, std::vector<std::string>>> cmdVals_;
    int chosenCmd_ = 0;

    // copied from ArgParser
    std::map<int, std::vector<Flag>> cmdFlags_;

    friend class ArgParser;
};

} // namespace openzl::arg
