// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * SDDL2 Assembler CLI
 *
 * Assembles SDDL2 assembly language to SDDL2 VM bytecode.
 *
 * Usage:
 *   sddl2_assembler -i <input.asm> [output.bin]
 *   sddl2_assembler -c '<assembly code>' [output.bin]
 */

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include "openzl/cpp/poly/StringView.hpp"

#include "tools/io/InputBuffer.h"
#include "tools/io/InputFile.h"
#include "tools/io/OutputFile.h"
#include "tools/sddl2/assembler/Assembler.h"

using namespace openzl;
using namespace openzl::sddl2;

namespace {
void printHexDump(const std::vector<uint8_t>& bytecode)
{
    for (size_t i = 0; i < bytecode.size(); ++i) {
        if (i > 0) {
            std::cout << ' ';
        }
        std::cout << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(bytecode[i]);
    }
    std::cout << '\n';
}

const char* const help_message =
        "SDDL Assembler \n"
        "\n"
        "Options:\n"
        "  -h  Print this help message.\n"
        "  -c  Assemble code from command line string. If no output is specified, bytecode will be printed to stdout.\n"
        "  -i  Input file. If no output is specified, bytecode will be written to <input>.bin.\n"
        "  -o  Output file.\n";

} // namespace

int main(int argc, char* argv[])
{
    int verbosity                             = 0;
    std::shared_ptr<tools::io::Input> input   = nullptr;
    std::shared_ptr<tools::io::Output> output = nullptr;

    for (int i = 1; i < argc; i++) {
        if (argv[i] == std::string{ "-h" }) {
            std::cerr << help_message << std::endl;
            return 0;
        } else if (argv[i] == std::string{ "-c" }) {
            if (i + 1 >= argc) {
                std::cerr << "Missing code string." << std::endl;
                std::cerr << help_message << std::endl;
                return 1;
            }
            input = std::make_shared<tools::io::InputBuffer>(argv[++i], "");
        } else if (argv[i] == std::string{ "-i" }) {
            if (i + 1 >= argc) {
                std::cerr << "Missing input file." << std::endl;
                std::cerr << help_message << std::endl;
                return 1;
            }
            input = std::make_shared<tools::io::InputFile>(argv[++i]);
        } else if (argv[i] == std::string{ "-o" }) {
            if (i + 1 >= argc) {
                std::cerr << "Missing output file." << std::endl;
                std::cerr << help_message << std::endl;
                return 1;
            }
            output = std::make_shared<tools::io::OutputFile>(argv[++i]);
        } else {
            std::cerr << "Unrecognized option." << std::endl;
            std::cerr << help_message << std::endl;
            return 1;
        }
    }

    try {
        if (!input) {
            throw std::runtime_error(
                    "No input provided (either with -c or -i).");
        }
        if (!output && !input->name().empty()) {
            std::filesystem::path path{ std::string(input->name()) };
            output = std::make_shared<tools::io::OutputFile>(
                    path.replace_extension("bin").string());
        }
        const auto assembler = sddl2::Assembler();
        const auto bytecode  = assembler.assemble(input->contents());

        if (output) {
            const auto sv = poly::string_view{ (const char*)bytecode.data(),
                                               bytecode.size() };
            output->write(sv);
            std::cout << "Assembled " << bytecode.size() << " bytes to "
                      << output->name() << std::endl;
        } else {
            printHexDump(bytecode);
        }

    } catch (const AssemblerError& e) {
        if (verbosity >= -1) {
            std::cerr << "Assembling failed:" << std::endl;
            std::cerr << e.what() << std::endl;
        }
        return 1;
    } catch (const std::exception& e) {
        if (verbosity >= -1) {
            std::cerr << "Error:" << std::endl;
            std::cerr << e.what() << std::endl;
        }
        return 1;
    }
    return 0;
}
