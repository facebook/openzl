// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/protobuf/DescriptorLoader.h"
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace openzl {
namespace protobuf {

DescriptorLoader::DescriptorLoader()
        : error_collector_(std::make_unique<ErrorCollector>())
{
}

DescriptorLoader::~DescriptorLoader() = default;

void DescriptorLoader::addProtoPath(const std::string& path)
{
    proto_paths_.push_back(path);
}

std::unique_ptr<google::protobuf::DescriptorPool>
DescriptorLoader::loadProtoFile(const std::string& proto_file)
{
    // Create source tree for import resolution
    google::protobuf::compiler::DiskSourceTree source_tree;

    // Map virtual path to proto_paths
    for (const auto& path : proto_paths_) {
        source_tree.MapPath("", path);
    }

    // Create importer
    google::protobuf::compiler::Importer importer(
            &source_tree, error_collector_.get());

    // Import the main proto file
    const auto* file_desc = importer.Import(proto_file);

    // Check for errors
    if (file_desc == nullptr || !error_collector_->errors().empty()) {
        std::ostringstream oss;
        oss << "Failed to parse " << proto_file << ":\n";
        for (const auto& error : error_collector_->errors()) {
            oss << "  " << error << "\n";
        }
        throw std::runtime_error(oss.str());
    }

    // Create a new DescriptorPool and copy descriptors
    // Note: We need to transfer ownership from the importer's pool
    // to our own pool using FileDescriptorSet
    google::protobuf::FileDescriptorSet file_desc_set;

    // Build FileDescriptorSet from the importer's pool
    // We need to copy all files in dependency order
    std::vector<const google::protobuf::FileDescriptor*> files_to_copy;
    std::set<const google::protobuf::FileDescriptor*> visited;

    // Helper to recursively add dependencies
    std::function<void(const google::protobuf::FileDescriptor*)> add_with_deps;
    add_with_deps = [&](const google::protobuf::FileDescriptor* fd) {
        if (visited.find(fd) != visited.end()) {
            return;
        }
        visited.insert(fd);

        // Add dependencies first
        for (int i = 0; i < fd->dependency_count(); ++i) {
            add_with_deps(fd->dependency(i));
        }

        // Then add the file itself
        files_to_copy.push_back(fd);
    };

    add_with_deps(file_desc);

    for (const auto* fd : files_to_copy) {
        fd->CopyTo(file_desc_set.add_file());
    }

    // Create new pool and build from FileDescriptorSet
    auto pool = std::make_unique<google::protobuf::DescriptorPool>();
    for (const auto& file_proto : file_desc_set.file()) {
        const auto* built_file = pool->BuildFile(file_proto);
        if (built_file == nullptr) {
            throw std::runtime_error(
                    "Failed to build descriptor for " + file_proto.name());
        }
    }

    return pool;
}

std::unique_ptr<google::protobuf::DescriptorPool>
DescriptorLoader::loadDescriptorFile(const std::string& desc_file)
{
    // Read the descriptor file
    std::ifstream file(desc_file, std::ios::binary);
    if (!file) {
        throw std::runtime_error(
                "Failed to open descriptor file: " + desc_file);
    }

    // Parse FileDescriptorSet
    google::protobuf::FileDescriptorSet file_desc_set;
    if (!file_desc_set.ParseFromIstream(&file)) {
        throw std::runtime_error(
                "Failed to parse descriptor file: " + desc_file);
    }

    // Create DescriptorPool and build all descriptors
    auto pool = std::make_unique<google::protobuf::DescriptorPool>();

    for (const auto& file_proto : file_desc_set.file()) {
        const auto* file_desc = pool->BuildFile(file_proto);
        if (file_desc == nullptr) {
            throw std::runtime_error(
                    "Failed to build descriptor for " + file_proto.name());
        }
    }

    return pool;
}

} // namespace protobuf
} // namespace openzl
