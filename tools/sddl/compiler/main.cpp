// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <iostream>
#include <iterator>
#include <string>

#include "tools/sddl/compiler/Compiler.h"
#include "tools/sddl/compiler/Exception.h"

using namespace openzl::sddl;

namespace {

const char* const help_message =
        "SDDL Compiler for OpenZL\n"
        "\n"
        "Options:\n"
        "  -h  Print this help message.\n"
        "  -v  Increase verbosity.\n"
        "  -q  Decrease verbosity.\n";

}

int main(int argc, char* argv[])
{
    int verbosity = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i] == std::string{ "-v" }) {
            verbosity++;
        } else if (argv[i] == std::string{ "-q" }) {
            verbosity--;
        } else if (argv[i] == std::string{ "-h" }) {
            std::cerr << help_message << std::endl;
            return 0;
        } else {
            std::cerr << "Unrecognized option." << std::endl;
            std::cerr << help_message << std::endl;
            return 1;
        }
    }

    try {
        std::istreambuf_iterator<char> begin{ std::cin }, end;
        const auto input    = std::string{ begin, end };
        const auto compiler = Compiler{ std::cerr, verbosity };
        const auto compiled = compiler.compile(input, "[stdin]");
        std::cout << compiled;
    } catch (const CompilerException& ex) {
        if (verbosity >= -1) {
            std::cerr << "Compilation failed:\n";
            std::cerr << ex.what();
        }
        return 1;
    }
    return 0;
}
