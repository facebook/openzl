// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/io/InputSet.h"
#include <stdexcept>

namespace openzl::tools::io {

InputSet::Iterator InputSet::begin() const { return Iterator{begin_state()}; }

InputSet::Iterator InputSet::end() const { return Iterator{}; }

// end()
InputSet::Iterator::Iterator() : state_() {}

// begin()
InputSet::Iterator::Iterator(std::unique_ptr<IteratorState> state)
    : state_((state && **state) ? std::move(state)
                                : std::unique_ptr<IteratorState>{}) {}

InputSet::Iterator::Iterator(const Iterator &o)
    : state_(o.state_ ? o.state_->copy() : std::unique_ptr<IteratorState>{}) {}

InputSet::Iterator &InputSet::Iterator::operator=(const Iterator &o) {
  state_ = o.state_ ? o.state_->copy() : std::unique_ptr<IteratorState>{};
  return *this;
}

const std::shared_ptr<Input> &InputSet::Iterator::operator*() const {
  static const std::shared_ptr<Input> null_input{};
  if (!state_) {
    throw std::runtime_error("Can't deref end InputSet::Iterator.");
  }
  return **state_;
}

InputSet::Iterator &InputSet::Iterator::operator++() {
  if (!state_) {
    throw std::runtime_error(
        "Can't advance InputSet::Iterator past the end of the InputSet.");
  }
  ++(*state_);
  if (!**state_) {
    state_.reset();
  }
  return *this;
}

InputSet::Iterator InputSet::Iterator::operator++(int) const {
  Iterator new_it{*this};
  ++new_it;
  return new_it;
}

bool InputSet::Iterator::operator==(const Iterator &o) const {
  return (!state_ && !o.state_) || (state_ && o.state_ && *state_ == *o.state_);
}

bool InputSet::Iterator::operator!=(const Iterator &o) const {
  return !(*this == o);
}

} // namespace openzl::tools::io
