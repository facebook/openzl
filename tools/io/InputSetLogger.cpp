// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/io/InputSetLogger.h"
#include "tools/io/IOException.h"

#include <iostream>

namespace openzl::tools::io {

class InputSetLogger::IteratorStateLogger : public InputSet::IteratorState {
   private:
    class IndentGuard {
       private:
        static int indents;

       public:
        IndentGuard()
        {
            indents++;
        }

        ~IndentGuard()
        {
            indents--;
        }

        operator std::string() const
        {
            return std::string((size_t)indents, ' ');
        }

        std::string operator*() const
        {
            return (std::string) * this;
        }
    };

   public:
    IteratorStateLogger(const InputSetLogger& isl, int verbosity)
            : isl_(isl), verbosity_(verbosity), it_(isl_.input_set_->begin())
    {
        IndentGuard ig;
        std::cerr << *ig << "InputSet::IteratorState " << this << ": "
                  << __PRETTY_FUNCTION__ << std::endl;
    }

    std::unique_ptr<IteratorState> copy() const override
    {
        if (verbosity_ >= 2) {
            IndentGuard ig;
            std::cerr << *ig << "InputSet::IteratorState " << this << ": "
                      << __PRETTY_FUNCTION__ << std::endl;
        }
        return std::make_unique<IteratorStateLogger>(*this);
    }

    IteratorState& operator++() override
    {
        if (verbosity_ >= 2) {
            IndentGuard ig;
            std::cerr << *ig << "InputSet::IteratorState " << this << ": "
                      << __PRETTY_FUNCTION__ << std::endl;
        }
        ++it_;
        return *this;
    }

    const std::shared_ptr<Input>& operator*() const override
    {
        IndentGuard ig;
        if (it_ == end_) {
            static const std::shared_ptr<Input> null_input{};
            std::cerr << *ig << "InputSet::IteratorState " << this << ": "
                      << __PRETTY_FUNCTION__ << " which is empty." << std::endl;
            return null_input;
        }
        const auto& input_ptr = *it_;
        std::cerr << *ig << "InputSet::IteratorState " << this << ": "
                  << __PRETTY_FUNCTION__ << " returns " << input_ptr.get()
                  << " which represents '"
                  << (input_ptr ? input_ptr->name() : "(nil)") << "'"
                  << std::endl;
        return input_ptr;
    }

    bool operator==(const IteratorState& o) const override
    {
        IndentGuard ig;
        auto ptr = dynamic_cast<const IteratorStateLogger*>(&o);
        if (ptr == nullptr) {
            return false;
        }
        std::cerr << *ig << "InputSet::IteratorState " << this << ": "
                  << __PRETTY_FUNCTION__ << " on " << &o;
        return (&isl_ == &ptr->isl_) && (it_ == ptr->it_);
    }

   private:
    const InputSetLogger& isl_;
    const int verbosity_;
    InputSet::Iterator it_;
    InputSet::Iterator end_;
};

int InputSetLogger::IteratorStateLogger::IndentGuard::indents{ -1 };

InputSetLogger::InputSetLogger(
        std::unique_ptr<InputSet> input_set,
        int verbosity)
        : input_set_(std::move(input_set)), verbosity_(verbosity)
{
    if (!input_set_) {
        throw IOException(
                "InputSet passed into InputSetLogger() cannot be nullptr!");
    }
}

std::unique_ptr<InputSet::IteratorState> InputSetLogger::begin_state() const
{
    return std::make_unique<IteratorStateLogger>(*this, verbosity_);
}

} // namespace openzl::tools::io
