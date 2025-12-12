// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

namespace openzl {
namespace tests {

// Maximum number of nodes to include in a graph when creating a random graph
// for testing / fuzzing. This is to avoid running into ZStrong limits on graph
// sizes.
// TODO(terrelln): Increase this to 100 after making a release.
static size_t constexpr kMaxNodesInGraph = 20;

/// Maximum depth of a randomly created graph for testing / fuzzing. This is to
/// avoid exponential behavior of variable-output nodes.
static size_t constexpr kMaxGraphDepth = 4;

} // namespace tests
} // namespace openzl
