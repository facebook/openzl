// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "src/openzl/compress/graphs/sddl2/sddl2_vm.h"

#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/codegen/RegisterAllocator.h"

namespace openzl::sddl2 {

size_t RegisterAllocator::allocate()
{
    size_t reg;
    if (!free_registers_.empty()) {
        auto free_it = free_registers_.begin();
        reg          = *free_it;
        free_registers_.erase(free_it);
    } else {
        reg = next_register_++;
        if (reg >= SDDL2_VAR_REGISTER_COUNT) {
            throw CodegenError(
                    "Too many registers! Maximum number of registers is "
                    + std::to_string(SDDL2_VAR_REGISTER_COUNT));
        }
    }
    return reg;
}

void RegisterAllocator::free(size_t reg)
{
    free_registers_.insert(reg);
}

size_t RegisterAllocator::assign(const std::string& name)
{
    auto it = var_registry_.find(name);
    if (it != var_registry_.end()) {
        return it->second;
    }

    size_t reg          = allocate();
    var_registry_[name] = reg;
    return reg;
}

void RegisterAllocator::unassign(const std::string& name)
{
    auto it = var_registry_.find(name);
    if (it != var_registry_.end()) {
        free(it->second);
        var_registry_.erase(it);
    }
}

size_t RegisterAllocator::get(const std::string& name)
{
    auto it = var_registry_.find(name);
    if (it == var_registry_.end()) {
        throw CodegenError("Undefined variable! '" + name + "'");
    }
    return it->second;
}

} // namespace openzl::sddl2
