// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include <string>
#include <unordered_set>

#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {

/**
 * Manages register allocation for the SDDL2 code generator.
 */
class RegisterAllocator {
   public:
    /**
     * Allocates an anonymous register (not associated with a variable name).
     *
     * @returns The allocated register number.
     * @throws CodegenError if the maximum number of registers is exceeded.
     */
    size_t allocate();

    /**
     * Frees a register so it can be reused.
     *
     * @param reg The register number to free.
     */
    void free(size_t reg);

    /**
     * Assigns a register to a named variable.
     *
     * @param name The variable name.
     * @returns The assigned register number.
     * @throws CodegenError if the maximum number of registers is exceeded.
     */
    size_t assign(const std::string& name);

    /**
     * Unassigns a named variable's register, freeing it for reuse.
     *
     * @param name The variable name to unassign.
     */
    void unassign(const std::string& name);

    /**
     * Gets the register for a variable.
     *
     * @param name The variable name.
     * @returns The register number.
     * @throws CodegenError if the variable is undefined.
     */
    size_t get(const std::string& name);

   private:
    std::map<std::string, size_t> var_registry_;
    std::unordered_set<size_t> free_registers_;
    size_t next_register_ = 0;
};

} // namespace openzl::sddl2
