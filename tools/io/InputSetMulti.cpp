// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/io/InputSetMulti.h"

#include <stdexcept>

namespace openzl::tools::io {

class InputSetMulti::IteratorStateMulti : public InputSet::IteratorState {
   public:
    explicit IteratorStateMulti(const InputSetMulti& ism)
            : ism_(ism), idx_(0), size_(ism.input_sets_.size())
    {
        if (idx_ != size_) {
            inner_it_  = ism_[idx_].begin();
            inner_end_ = ism_[idx_].end();
        }
        advance_to_next_nonempty();
    }

    std::unique_ptr<IteratorState> copy() const override
    {
        return std::make_unique<IteratorStateMulti>(*this);
    }

    IteratorState& operator++() override
    {
        if (idx_ == size_) {
            throw std::runtime_error(
                    "Can't advance iterator past the end of the InputSet.");
        }
        if (inner_it_ != inner_end_) {
            ++inner_it_;
        }
        if (inner_it_ == inner_end_) {
            idx_++;
            if (idx_ != size_) {
                inner_it_  = ism_[idx_].begin();
                inner_end_ = ism_[idx_].end();
            }
            advance_to_next_nonempty();
        }
        return *this;
    }

    const std::shared_ptr<Input>& operator*() const override
    {
        static const std::shared_ptr<Input> null_input{};
        if (idx_ == ism_.input_sets_.size()) {
            return null_input;
        }
        if (inner_it_ == inner_end_) {
            return null_input;
        }
        return *inner_it_;
    }

    bool operator==(const IteratorState& o) const override
    {
        auto ptr = dynamic_cast<const IteratorStateMulti*>(&o);
        if (ptr == nullptr) {
            return false;
        }
        return (&ism_ == &ptr->ism_) && (idx_ == ptr->idx_)
                && (inner_it_ == ptr->inner_it_);
    }

   private:
    void advance_to_next_nonempty()
    {
        if (idx_ == size_) {
            return;
        }
        while (inner_it_ == inner_end_) {
            idx_++;
            if (idx_ == size_) {
                break;
            }
            inner_it_  = ism_[idx_].begin();
            inner_end_ = ism_[idx_].end();
        }
    }

    const InputSetMulti& ism_;
    size_t idx_;
    const size_t size_;

    InputSet::Iterator inner_it_;
    InputSet::Iterator inner_end_;
};

InputSetMulti::InputSetMulti(std::vector<std::unique_ptr<InputSet>> input_sets)
        : input_sets_(std::move(input_sets))
{
}

const InputSet& InputSetMulti::operator[](size_t idx) const
{
    return *input_sets_[idx];
}

std::unique_ptr<InputSet::IteratorState> InputSetMulti::begin_state() const
{
    return std::make_unique<IteratorStateMulti>(*this);
}

} // namespace openzl::tools::io
