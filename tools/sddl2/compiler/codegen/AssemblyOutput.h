// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <list>
#include <sstream>
#include <string>

namespace openzl::sddl2 {

/**
 * Output of the code generator: a sequence of assembly instructions.
 */
using Instruction = std::string;

class AssemblyOutput {
   public:
    void operator+=(AssemblyOutput&& other)
    {
        insts_.splice(insts_.end(), other.insts_);
    }

    void operator+=(std::string&& inst)
    {
        insts_.emplace_back(std::move(inst));
    }

    void operator+=(const std::string& inst)
    {
        insts_.emplace_back(inst);
    }

    std::string str() const
    {
        std::ostringstream oss;
        for (const auto& inst : insts_) {
            oss << inst << std::endl;
        }
        return std::move(oss).str();
    }

    size_t size() const
    {
        return insts_.size();
    }

   private:
    std::list<Instruction> insts_;
};

} // namespace openzl::sddl2
