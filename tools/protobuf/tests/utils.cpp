// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "tools/protobuf/tests/utils.h"

namespace openzl {
namespace protobuf {

std::filesystem::path getTestDataPath(const std::string& filename)
{
#ifdef ZL_IS_FBCODE
    auto path = std::filesystem::path("resources") / filename;
    return build::getResourcePath(path.string()).string();
#else
    // Use __FILE__ to derive the test data directory
    std::string this_file = __FILE__;
    std::filesystem::path this_path(this_file);
    std::filesystem::path test_dir = this_path.parent_path();
    return (test_dir / "resources" / filename);
#endif
}

} // namespace protobuf
} // namespace openzl
