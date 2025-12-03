// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/arg/parsed_args.h"

#include <algorithm>
#include <stdexcept>

#include "tools/arg/ParseException.h"
#include "tools/arg/arg_parser.h"

namespace openzl::arg {
namespace {
std::optional<std::string> vecToOptional(const std::vector<std::string>& vec)
{
    if (vec.size() == 1) {
        return vec[0];
    }
    return std::nullopt;
}
} // namespace

int ParsedArgs::chosenCmd() const
{
    return chosenCmd_;
}

std::optional<std::string> ParsedArgs::immediate() const
{
    // check global flags first
    if (cmdVals_.find(ArgParser::CMD_UNSPECIFIED) != cmdVals_.end()) {
        for (const auto& kv : cmdVals_.at(ArgParser::CMD_UNSPECIFIED)) {
            const auto& flagVec = cmdFlags_.at(ArgParser::CMD_UNSPECIFIED);
            if (std::find_if(
                        flagVec.begin(),
                        flagVec.end(),
                        [&](const Flag& flag) {
                            return flag.name == kv.first && flag.immediate;
                        })
                != flagVec.end()) {
                return kv.first;
            }
        }
    }
    // skip command flag check if this command has no registered flags or values
    if (cmdFlags_.find(chosenCmd_) == cmdFlags_.end()
        || cmdVals_.find(chosenCmd_) == cmdVals_.end()) {
        return std::nullopt;
    }

    // otherwise check command flags
    for (const auto& kv : cmdVals_.at(chosenCmd_)) {
        const auto& flagVec = cmdFlags_.at(chosenCmd_);
        if (std::find_if(
                    flagVec.begin(),
                    flagVec.end(),
                    [&kv](const Flag& flag) {
                        return flag.name == kv.first && flag.immediate;
                    })
            != flagVec.end()) {
            return kv.first;
        }
    }
    return std::nullopt;
}

bool ParsedArgs::globalHasFlag(const std::string& name) const
{
    return cmdHasFlag(ArgParser::CMD_UNSPECIFIED, name);
}

std::optional<std::string> ParsedArgs::globalFlag(const std::string& name) const
{
    if (cmdVals_.find(ArgParser::CMD_UNSPECIFIED) == cmdVals_.end()) {
        return std::nullopt;
    }
    auto& mp = cmdVals_.at(ArgParser::CMD_UNSPECIFIED);
    if (mp.find(name) == mp.end()) {
        return std::nullopt;
    }
    return vecToOptional(mp.at(name));
}

std::string ParsedArgs::globalRequiredFlag(const std::string& name) const
{
    return cmdRequiredFlag(ArgParser::CMD_UNSPECIFIED, name);
}

std::vector<std::string> ParsedArgs::cmdPositionals(
        int cmd,
        const std::string& name) const
{
    auto mp = cmdVals_.at(cmd);
    if (mp.find(name) == mp.end()) {
        throw ParseException("No positional arg with name " + name);
    }
    return mp.at(name);
}

std::string ParsedArgs::cmdPositional(int cmd, const std::string& name) const
{
    auto pos = vecToOptional(cmdPositionals(cmd, name));
    if (!pos.has_value()) {
        throw ParseException(
                "Need exactly one positional arg with name " + name);
    }
    return pos.value();
}

bool ParsedArgs::cmdHasFlag(int cmd, const std::string& name) const
{
    if (cmdVals_.find(cmd) == cmdVals_.end()) {
        return false;
    }
    auto& mp = cmdVals_.at(cmd);
    return mp.find(name) != mp.end();
}

std::optional<std::string> ParsedArgs::cmdFlag(int cmd, const std::string& name)
        const
{
    auto& mp = cmdVals_.at(cmd);
    if (mp.find(name) == mp.end()) {
        return std::nullopt;
    }
    return vecToOptional(mp.at(name));
}

std::string ParsedArgs::cmdRequiredFlag(int cmd, const std::string& name) const
{
    auto opt = cmdFlag(cmd, name);
    if (!opt.has_value()) {
        throw ParseException("Please specify a value for --" + name);
    }
    return opt.value();
}

} // namespace openzl::arg
