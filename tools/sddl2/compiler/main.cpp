// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <filesystem>
#include <string>

#include "tools/sddl2/compiler/Compiler.h"
#include "tools/sddl2/compiler/Exception.h"

#include "tools/io/InputFile.h"
#include "tools/io/OutputFile.h"

using namespace openzl;
using namespace openzl::sddl2;

namespace {

const char* const help_message =
        "SDDL Compiler for OpenZL\n"
        "\n"
        "Options:\n"
        "  -h  Print this help message.\n"
        "  -v  Increase verbosity.\n"
        "  -q  Decrease verbosity.\n"
        "  -i  Input file.\n"
        "  -o  Output file.\n";

}

int main(int argc, char* argv[])
{
    int verbosity = 0;
    std::shared_ptr<tools::io::InputFile> input_file;
    std::shared_ptr<tools::io::OutputFile> output_file;
    for (int i = 1; i < argc; i++) {
        if (argv[i] == std::string{ "-v" }) {
            verbosity++;
        } else if (argv[i] == std::string{ "-q" }) {
            verbosity--;
        } else if (argv[i] == std::string{ "-h" }) {
            std::cerr << help_message << std::endl;
            return 0;
        } else if (argv[i] == std::string{ "-i" }) {
            if (i + 1 >= argc) {
                std::cerr << "Missing input file." << std::endl;
                std::cerr << help_message << std::endl;
                return 1;
            }
            input_file = std::make_shared<tools::io::InputFile>(argv[++i]);
        } else if (argv[i] == std::string{ "-o" }) {
            if (i + 1 >= argc) {
                std::cerr << "Missing output file." << std::endl;
                std::cerr << help_message << std::endl;
                return 1;
            }
            output_file = std::make_shared<tools::io::OutputFile>(argv[++i]);
        } else {
            std::cerr << "Unrecognized option." << std::endl;
            std::cerr << help_message << std::endl;
            return 1;
        }
    }

    try {
        if (!input_file) {
            throw std::runtime_error("No input file specified.");
        }
        if (!output_file) {
            std::filesystem::path path{ std::string(input_file->name()) };
            output_file = std::make_shared<tools::io::OutputFile>(
                    path.replace_extension("asm"));
        }
        const auto compiler =
                Compiler{ Compiler::Options{}.with_verbosity(verbosity) };
        const auto compiled =
                compiler.compile(input_file->contents(), input_file->name());

        output_file->write(compiled);
    } catch (const CompilerException& ex) {
        if (verbosity >= -1) {
            std::cerr << "Compilation failed:\n";
            std::cerr << ex.what();
        }
        return 1;
    }
    return 0;
}
