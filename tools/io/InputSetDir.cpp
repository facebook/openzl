// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/io/InputSetDir.h"

#include <filesystem>
#include <stdexcept>


#include "tools/io/InputFile.h"

namespace openzl::tools::io {

template <typename IterT>
class InputSetDir::IteratorStateDir : public InputSet::IteratorState {
   public:
    IteratorStateDir(const InputSetDir& isd, const std::string& path)
            : isd_(isd), it_(path)
    {
        advance_to_next_regular_file();
    }

    std::unique_ptr<IteratorState> copy() const override
    {
        return std::make_unique<IteratorStateDir>(*this);
    }

    // Advance to the next regular file.
    IteratorState& operator++() override
    {
        input_.reset();
        if (it_ == IterT{}) {
            throw std::runtime_error(
                    "Can't advance iterator past the end of the InputSet.");
        }
        ++it_;
        advance_to_next_regular_file();
        return *this;
    }

    const std::shared_ptr<Input>& operator*() const override
    {
        if (input_) {
            return input_;
        }
        if (it_ == IterT{}) {
            return input_;
        }
        input_ = std::make_shared<InputFile>(it_->path().string());
        return input_;
    }

    bool operator==(const IteratorState& o) const override
    {
        auto ptr = dynamic_cast<const IteratorStateDir*>(&o);
        if (ptr == nullptr) {
            return false;
        }
        return (&isd_ == &ptr->isd_) && (it_ == ptr->it_);
    }

   private:
    void advance_to_next_regular_file()
    {
        while (true) {
            if (it_ == IterT{}) {
                break;
            }
            if (it_->is_regular_file()) {
                break;
            }
            ++it_;
        }
    }

    const InputSetDir& isd_;
    IterT it_;
    mutable std::shared_ptr<Input> input_;
};

InputSetDir::InputSetDir(std::string path, bool recursive)
        : path_(std::move(path)), recursive_(recursive)
{
}

std::unique_ptr<InputSet::IteratorState> InputSetDir::begin_state() const
{
    if (recursive_) {
        return std::make_unique<IteratorStateDir<
                std::filesystem::recursive_directory_iterator>>(*this, path_);
    } else {
        return std::make_unique<
                IteratorStateDir<std::filesystem::directory_iterator>>(
                *this, path_);
    }
}

} // namespace openzl::tools::io
