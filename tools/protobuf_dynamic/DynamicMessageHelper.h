// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <string>

namespace google {
namespace protobuf {
class DescriptorPool;
class DynamicMessageFactory;
class Message;
class Descriptor;
} // namespace protobuf
} // namespace google

namespace openzl {
namespace protobuf {

/**
 * Helper class to manage the lifecycle of dynamic protobuf messages.
 *
 * This class provides a simple interface for creating and working with
 * protobuf messages whose schemas are loaded at runtime rather than
 * compiled into the binary.
 */
class DynamicMessageHelper {
   public:
    /**
     * Construct a helper with the given descriptor pool.
     * The pool must outlive this helper instance.
     *
     * @param pool Descriptor pool containing message definitions
     */
    explicit DynamicMessageHelper(const google::protobuf::DescriptorPool* pool);

    ~DynamicMessageHelper();

    // Disable copy to avoid ownership issues
    DynamicMessageHelper(const DynamicMessageHelper&)            = delete;
    DynamicMessageHelper& operator=(const DynamicMessageHelper&) = delete;

    // Enable move
    DynamicMessageHelper(DynamicMessageHelper&&) noexcept;
    DynamicMessageHelper& operator=(DynamicMessageHelper&&) noexcept;

    /**
     * Create a new message instance by fully qualified type name.
     *
     * @param type_name Fully qualified message type (e.g.,
     * "mypackage.MyMessage")
     * @return Unique pointer to new message, or nullptr if type not found
     */
    std::unique_ptr<google::protobuf::Message> newMessage(
            const std::string& type_name) const;

    /**
     * Parse binary protobuf data into a dynamic message.
     *
     * @param type_name Fully qualified message type
     * @param data Binary protobuf data
     * @param size Size of binary data
     * @return Unique pointer to parsed message, or nullptr on error
     */
    std::unique_ptr<google::protobuf::Message> parseMessage(
            const std::string& type_name,
            const void* data,
            size_t size) const;

    /**
     * Get the descriptor for a message type.
     *
     * @param type_name Fully qualified message type
     * @return Descriptor pointer, or nullptr if not found
     */
    const google::protobuf::Descriptor* getDescriptor(
            const std::string& type_name) const;

    /**
     * Get the underlying descriptor pool.
     */
    const google::protobuf::DescriptorPool* getPool() const
    {
        return pool_;
    }

   private:
    const google::protobuf::DescriptorPool* pool_;
    std::unique_ptr<google::protobuf::DynamicMessageFactory> factory_;
};

} // namespace protobuf
} // namespace openzl
