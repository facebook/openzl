// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/io/InputSetStatic.h"
#include <stdexcept>

namespace openzl::tools::io {

class InputSetStatic::IteratorStateStatic : public InputSet::IteratorState {
public:
  explicit IteratorStateStatic(const InputSetStatic &iss, size_t idx)
      : iss_(iss), idx_(idx) {}

  std::unique_ptr<IteratorState> copy() const override {
    return std::make_unique<IteratorStateStatic>(iss_, idx_);
  }

  IteratorState &operator++() override {
    idx_++;
    return *this;
  }

  const std::shared_ptr<Input> &operator*() const override {
    return iss_[idx_];
  }

  bool operator==(const IteratorState &o) const override {
    auto ptr = dynamic_cast<const IteratorStateStatic *>(&o);
    if (ptr == nullptr) {
      return false;
    }
    return (&iss_ == &ptr->iss_) && (idx_ == ptr->idx_);
  }

private:
  const InputSetStatic &iss_;
  size_t idx_{0};
};

InputSetStatic::InputSetStatic(std::vector<std::shared_ptr<Input>> inputs)
    : inputs_(std::move(inputs)) {
  for (const auto &input : inputs_) {
    if (!input) {
      throw std::runtime_error("InputSetStatic cannot hold a nullptr.");
    }
  }
}

InputSetStatic InputSetStatic::from_input_set(InputSet &input_set) {
  std::vector<std::shared_ptr<Input>> inputs{input_set.begin(),
                                             input_set.end()};
  return InputSetStatic{std::move(inputs)};
}

size_t InputSetStatic::size() const { return inputs_.size(); }

const std::shared_ptr<Input> &InputSetStatic::operator[](size_t idx) const {
  static const std::shared_ptr<Input> null_input{};
  if (idx < inputs_.size()) {
    return inputs_[idx];
  }
  return null_input;
}

std::unique_ptr<InputSet::IteratorState> InputSetStatic::begin_state() const {
  return std::make_unique<IteratorStateStatic>(*this, 0);
}

} // namespace openzl::tools::io
