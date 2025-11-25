// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/Tokenizer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "openzl/cpp/poly/Optional.hpp"

#include "tools/sddl2/compiler/Exception.h"

using namespace openzl::sddl2::detail;

namespace openzl::sddl2 {

namespace {
class TokenizerImpl {
   public:
    explicit TokenizerImpl(const Source& source, const Logger& logger)
            : source_(source), log_(logger)
    {
    }

   private:
    SourceLocation loc(poly::string_view token) const
    {
        return source_.location(token);
    }

    poly::optional<Token> consume_ws(poly::string_view& input) const
    {
        poly::string_view nl{};
        while (!input.empty()) {
            if (std::isspace(input[0])) {
                if (input[0] == '\n' && nl.empty()) {
                    nl = input.substr(0, 1);
                }
                input.remove_prefix(1);
            } else if (input[0] == '#') {
                consume_comment(input);
            } else {
                break;
            }
        }
        if (!nl.empty()) {
            return Token{ loc(nl), Symbol::NL };
        }
        return poly::nullopt;
    }

    void consume_comment(poly::string_view& input) const
    {
        while (!input.empty() && input[0] != '\n') {
            input.remove_prefix(1);
        }
    }

    bool is_first_word_char(const poly::string_view& input) const
    {
        if (input.empty()) {
            return false;
        }
        const uint8_t c = input[0];
        return std::isalpha(c) || c == '_';
    }

    bool is_word_char(const poly::string_view& input) const
    {
        if (input.empty()) {
            return false;
        }
        const uint8_t c = input[0];
        return std::isalnum(c) || c == '_';
    }

    bool is_num_char(const poly::string_view& input) const
    {
        if (input.empty()) {
            return false;
        }
        const uint8_t c = input[0];
        return std::isalnum(c) || c == '.' || c == '_';
    }

    poly::optional<Token> match_symbol(const poly::string_view word) const
    {
        for (const auto& pair : strs_to_syms) {
            const auto& sym_str = pair.first;
            const auto& sym     = pair.second;
            if (word == sym_str) {
                return Token{ loc(word), sym };
            }
        }
        return std::nullopt;
    }

    Token read_word(poly::string_view& input) const
    {
        auto word = input;
        while (is_word_char(input)) {
            input.remove_prefix(1);
        }
        word = word.substr(0, word.size() - input.size());

        auto builtin = match_symbol(word);
        if (builtin) {
            return std::move(builtin).value();
        }

        return Token{ loc(word), word };
    }

    Token read_operator(poly::string_view& input) const
    {
        poly::string_view word;
        for (const auto& pair : strs_to_syms) {
            const auto& sym_str = pair.first;
            const auto& sym     = pair.second;
            word                = input.substr(0, sym_str.size());
            if (word == sym_str) {
                input.remove_prefix(sym_str.size());
                return Token{ loc(word), sym };
            }
        }
        throw SyntaxError(loc(input.substr(0, 1)), "Unrecognized operator!");
    }

    Token read_num(poly::string_view& input) const
    {
        poly::string_view num = input;
        while (is_num_char(input)) {
            input.remove_prefix(1);
        }
        num      = num.substr(0, num.size() - input.size());
        auto pos = loc(num);

        size_t read = 0;
        int64_t val;
        try {
            val = std::stoll(std::string{ num }, &read, 0);
        } catch (const std::out_of_range&) {
            throw SyntaxError(
                    pos, "Couldn't parse integer literal: out of range.");
        }
        if (read != num.size()) {
            throw SyntaxError(pos, "Couldn't parse integer literal.");
        }
        return Token{ std::move(pos), val };
    }

    Token read_token(poly::string_view& input) const
    {
        if (is_first_word_char(input)) {
            return read_word(input);
        }

        if (std::ispunct(input[0])) {
            return read_operator(input);
        }

        if (std::isdigit(input[0])) {
            return read_num(input);
        }

        throw SyntaxError(loc(input.substr(0, 1)), "Couldn't parse token");
    }

   public:
    std::vector<Token> tokenize() const
    {
        auto source = source_.contents();
        std::vector<Token> tokens;
        while (true) {
            auto maybe_ws_token = consume_ws(source);
            if (maybe_ws_token.has_value()) {
                tokens.push_back(*maybe_ws_token);
            }
            if (source.empty()) {
                break;
            }
            tokens.push_back(read_token(source));
        }

        log_(1) << "Tokens:" << std::endl;
        for (const auto& token : tokens) {
            log_(1) << token.str(2);
        }
        log_(1) << std::endl;

        return tokens;
    }

   private:
    const Source& source_;
    const Logger& log_;
};
} // anonymous namespace

Tokenizer::Tokenizer(const Logger& logger) : log_(logger) {}

std::vector<Token> Tokenizer::tokenize(const Source& source) const
{
    return TokenizerImpl{ source, log_ }.tokenize();
}
} // namespace openzl::sddl2
