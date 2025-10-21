// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/protobuf/StringWriter.h"

namespace openzl {
namespace protobuf {

void StringWriter::write(const std::string& val)
{
    size_t written = 0;
    while (written < val.size()) {
        ensure(1); // New buffer if needed
        size_t len = std::min(val.size() - written, remaining());
        memcpy(ptr(), val.data() + written, len);
        written += len;
        pos_ += len;
    }
}

void StringWriter::init()
{
    bufs_.clear();
    bufs_.emplace_back(std::string(initLen_, 0));
    idx_ = 0;
    pos_ = 0;
}

char* StringWriter::ptr()
{
    return bufs_[idx_].data() + pos_;
}

size_t StringWriter::remaining() const
{
    if (bufs_.empty()) {
        return 0;
    }
    return bufs_[idx_].size() - pos_;
}

void StringWriter::grow()
{
    if (bufs_.empty()) {
        init();
        return;
    }
    bufs_.emplace_back(std::string(2 * bufs_[idx_].size(), 0));
    bufs_[idx_].resize(pos_);
    idx_++;
    pos_ = 0;
}

void StringWriter::ensure(size_t len)
{
    if (remaining() < len) {
        grow();
    }
}

std::string StringWriter::move()
{
    std::string ret;
    if (!bufs_.empty()) {
        // Resize the last buffer to the correct size
        bufs_[idx_].resize(pos_);

        size_t total = 0;
        for (auto& buf : bufs_) {
            total += buf.size();
        }

        // Copy the buffers into a single string
        ret        = std::move(bufs_[0]);
        size_t pos = ret.size();
        ret.resize(total);
        for (size_t i = 1; i < bufs_.size(); i++) {
            memcpy(ret.data() + pos, bufs_[i].data(), bufs_[i].size());
            pos += bufs_[i].size();
        }
    }
    init();
    return ret;
}
} // namespace protobuf
} // namespace openzl
