// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_COMPRESS_CNODE_H
#define OPENZL_COMPRESS_CNODE_H

#include "openzl/common/wire_format.h" // TransformType_e
#include "openzl/compress/compress_types.h" // InternalTransform_Desc, NodeType_e, FormatLimits
#include "openzl/compress/name.h"

typedef struct {
    NodeType_e nodetype;
    TransformType_e publicIDtype;
    // Minimum supported version number.
    // Currently only available for standard transforms.
    // TODO(terrelln): Should this be available for custom transforms?
    // What would the version number mean then?
    unsigned minFormatVersion;
    // Maximum supported version number.
    // Set to ZL_MAX_FORMAT_VERSION unless the node is deprecated.
    unsigned maxFormatVersion;
    InternalTransform_Desc transformDesc;
    /// If a dictionary is specified in the transformDesc, this field is
    /// populated with the index of that dict within the compressor's bundle.
    /// The "default" value is updated when ZL_Compressor_validate() is called,
    /// before compression starts.
    size_t maybeDictIndex /* defaults to ZL_DICT_INDEX_NONE */;
    /// Standard nodes leave this empty, all other nodes set this.
    /// When set ZL_Name_unique(&maybeName) == transformDesc.publicDesc.name.
    ZL_Name maybeName;
    /// In order for a graph to be serializable, we must be able to reconstruct
    /// functionally identical copies of all the nodes. Some nodes effectively
    /// exist a priori: standard nodes, obviously, as well as the nodes that
    /// result from registering a custom graph component. It's the engine's or
    /// the user's responsibility to make these nodes available under the same
    /// name on the new compressor.
    ///
    /// All other nodes, which are created by modifying an existing node, must
    /// record what that base node is, so that the serialization framework can
    /// recreate the node by looking up that node and applying the same
    /// overrides to in.
    ///
    /// This field records that reference to the node from which this node was
    /// created. Set to ZL_NODE_ILLEGAL when there is no such base node.
    ZL_NodeID baseNodeID;
} CNode;

/// @returns the local parameters for the @p cnode
/// @pre cnode->nodetype != node_store
const ZL_LocalParams* CNODE_getLocalParams(CNode const* cnode);

/// @returns the local int parameters for the @p cnode
/// @pre cnode->nodetype != node_store
ZL_LocalIntParams CNODE_getLocalIntParams(CNode const* cnode);

/// @returns the local generic parameters for the @p cnode
/// @pre cnode->nodetype != node_store
ZL_LocalCopyParams CNODE_getLocalCopyParams(CNode const* cnode);

/// @returns the local reference parameters for the @p cnode
/// @pre cnode->nodetype != node_store
ZL_LocalRefParams CNODE_getLocalRefParams(CNode const* cnode);

/// @returns the public transform info for the @p cnode
/// @pre cnode->nodetype == node_internalTransform
PublicTransformInfo CNODE_getTransformID(CNode const* cnode);

/// @returns If the provided @p node was created by modifying another existing
/// node, the `ZL_NodeID` of that other node. Otherwise, `ZL_NODE_ILLEGAL`.
ZL_NodeID CNODE_getBaseNodeID(CNode const* cnode);

/// @returns the unique name of the cnode
char const* CNODE_getName(CNode const* cnode);

/// @returns The ZL_Name object of the cnode.
/// @note Standard nodes don't fill maybeName, so this wraps the standard name
/// in a ZL_Name object.
ZL_Name CNODE_getNameObj(CNode const* cnode);

/// @returns the total number of Input Ports for the @p cnode.
/// Ports are declared at registration time, and represent one Input each,
/// except for the last one which may be variable. When the last port is
/// variable, the number of Ports can be different from the real number of
/// Inputs the Transform will receive at compression time.
/// @pre cnode->nodetype == node_internalTransform
size_t CNODE_getNbInputPorts(CNode const* cnode);

/// Tell if nbInputs is compatible with the node's declaration.
/// This is typically used at compression time, so nbInputs represents the real
/// number of Inputs considered for this Transform.
/// @returns 1 (true) if nbInputs is compatible, 0 (false) otherwise
/// @pre cnode->nodetype == node_internalTransform
bool CNODE_isNbInputsCompatible(const CNode* cnode, size_t nbInputs);

/// @returns True if the CNode takes a variable number of inputs.
bool CNODE_isVITransform(const CNode* cnode);

/// @returns the type for input @p inputIndex of node @p cnode
/// @pre cnode->nodetype == node_internalTransform
/// When inputIndex > CNODE_getNbInputPorts(), it returns the type of the last
/// input, which is valid for VI (Variable Input) nodes.
ZL_Type CNODE_getInputType(CNode const* cnode, ZL_IDType inputIndex);

/// @returns the total number of output outcomes for the @p cnode
///          including compulsory singletons and variable ones.
/// @pre cnode->nodetype == node_internalTransform
size_t CNODE_getNbOutcomes(CNode const* cnode);

/// @returns the number of singleton outputs for the @p cnode
/// @pre cnode->nodetype == node_internalTransform
size_t CNODE_getNbOut1s(CNode const* cnode);

/// @returns the number of variable outcomes for the @p cnode
/// @pre cnode->nodetype == node_internalTransform
size_t CNODE_getNbVOs(CNode const* cnode);

/// @returns tell if output outcome at index @p outStreamIndex
///  is of type VO (Variable Outcome).
///  Note : assumed to be compulsory Singleton output otherwise.
/// @pre cnode->nodetype == node_internalTransform
int CNODE_isVO(CNode const* cnode, int outStreamIndex);

/// @returns the stream type for the output stream
/// at index @p outStreamIndex of the @p cnode
/// @pre cnode must be valid
/// @pre cnode->nodetype == node_internalTransform
ZL_Type CNODE_getOutStreamType(CNode const* cnode, int outStreamIndex);

typedef FormatLimits CNODE_FormatInfo;

/// @returns The minimum supported format version for the given node.
/// TODO(terrelln): Currently only meaningful for standard nodes, custom nodes
/// always return ZL_MIN_FORMAT_VERSION.
CNODE_FormatInfo CNODE_getFormatInfo(CNode const* cnode);

// @returns the transformation type of the cnode
bool CNODE_isTransformStandard(CNode const* cnode);

/// @returns the dict ID associated with the @p cnode
/// @pre cnode->nodetype == node_internalTransform
ZL_DictID CNODE_getDictID(CNode const* cnode);

/// @returns the dict index within the compressor's bundle for the @p cnode,
/// or ZL_DICT_INDEX_NONE if no dictionary is associated.
/// @pre cnode->nodetype == node_internalTransform
size_t CNODE_getDictIndex(CNode const* cnode);

#endif // OPENZL_COMPRESS_CNODE_H
