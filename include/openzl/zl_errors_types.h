// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file
 *
 * This file defines some error types that are used both by the public header
 * and the detail / implementation header and that therefore need to be in a
 * separate header so they're accessible from both without recursive include
 * issues.
 */

#ifndef ZSTRONG_ZS2_ERRORS_TYPES_H
#define ZSTRONG_ZS2_ERRORS_TYPES_H

#include "openzl/zl_portability.h"

#if defined(__cplusplus)
extern "C" {
#endif

/************************
 * Forward Declarations *
 ************************/

typedef struct ZL_DynamicErrorInfo_s ZL_DynamicErrorInfo;
typedef struct ZL_StaticErrorInfo_s ZL_StaticErrorInfo;
typedef struct ZL_Error_s ZL_Error;
typedef union ZL_ErrorInfo_u ZL_ErrorInfo;

/*****************
 * ZL_ErrorCode *
 *****************/

/*-*********************************************
 *  Error codes list
 *-*********************************************
 *  note 1 : this API shall be used with static linking only.
 *           enum values are not stabilitized yet.
 *  note 2 : ZL_isError() is always correct, whatever the library version.
 **********************************************/
typedef enum {
    ZL_ErrorCode_no_error = 0,
    ZL_ErrorCode_GENERIC  = 1,
    /* user side errors */
    ZL_ErrorCode_srcSize_tooSmall              = 3,
    ZL_ErrorCode_srcSize_tooLarge              = 4,
    ZL_ErrorCode_dstCapacity_tooSmall          = 5,
    ZL_ErrorCode_userBuffer_alignmentIncorrect = 6,
    ZL_ErrorCode_decompression_incorrectAPI    = 7,
    ZL_ErrorCode_userBuffers_invalidNum        = 8,
    ZL_ErrorCode_invalidName                   = 9,
    /* frame level errors */
    ZL_ErrorCode_header_unknown             = 10,
    ZL_ErrorCode_frameParameter_unsupported = 11,
    ZL_ErrorCode_corruption                 = 12,
    ZL_ErrorCode_compressedChecksumWrong    = 13,
    ZL_ErrorCode_contentChecksumWrong       = 14,
    ZL_ErrorCode_outputs_tooNumerous        = 15,
    /* session errors */
    ZL_ErrorCode_compressionParameter_invalid         = 20,
    ZL_ErrorCode_parameter_invalid                    = 21,
    ZL_ErrorCode_outputID_invalid                     = 22,
    ZL_ErrorCode_invalidRequest_singleOutputFrameOnly = 23,
    ZL_ErrorCode_outputNotCommitted                   = 24,
    ZL_ErrorCode_outputNotReserved                    = 25,
    ZL_ErrorCode_segmenter_inputNotConsumed           = 26,
    /* graph stage errors */
    ZL_ErrorCode_graph_invalid          = 30,
    ZL_ErrorCode_graph_nonserializable  = 31,
    ZL_ErrorCode_invalidTransform       = 32,
    ZL_ErrorCode_graph_invalidNumInputs = 33,
    /* runtime compression errors */
    ZL_ErrorCode_successor_invalid          = 40,
    ZL_ErrorCode_successor_alreadySet       = 41,
    ZL_ErrorCode_successor_invalidNumInputs = 42,
    ZL_ErrorCode_inputType_unsupported      = 43,
    ZL_ErrorCode_graphParameter_invalid     = 44,
    /* runtime Node errors */
    ZL_ErrorCode_nodeParameter_invalid        = 50,
    ZL_ErrorCode_nodeParameter_invalidValue   = 51,
    ZL_ErrorCode_transform_executionFailure   = 52,
    ZL_ErrorCode_customNode_definitionInvalid = 53,
    ZL_ErrorCode_node_unexpected_input_type   = 54,
    ZL_ErrorCode_node_invalid_input           = 55,
    ZL_ErrorCode_node_invalid                 = 56,
    ZL_ErrorCode_nodeExecution_invalidOutputs = 57,
    ZL_ErrorCode_nodeRegen_countIncorrect     = 58,
    /* versioning errors */
    ZL_ErrorCode_formatVersion_unsupported = 60,
    ZL_ErrorCode_formatVersion_notSet      = 61,
    ZL_ErrorCode_node_versionMismatch      = 62,
    /* dictionary errors */
    ZL_ErrorCode_dict_corruption      = 65,
    ZL_ErrorCode_dict_materialization = 66,
    /* internal errors */
    ZL_ErrorCode_allocation              = 70,
    ZL_ErrorCode_internalBuffer_tooSmall = 71,
    ZL_ErrorCode_integerOverflow         = 72,
    ZL_ErrorCode_stream_wrongInit        = 73,
    ZL_ErrorCode_streamType_incorrect    = 74,
    ZL_ErrorCode_streamCapacity_tooSmall = 75,
    ZL_ErrorCode_streamParameter_invalid = 76,
    /* logic errors should never happen and will produce an assertion failure in
       debug builds */
    ZL_ErrorCode_logicError                 = 80,
    ZL_ErrorCode_temporaryLibraryLimitation = 81,
    ZL_ErrorCode_maxCode = 99 /* never EVER use this value directly,
                               * it's not stable, and can change in future
                               * versions! Use ZL_isError() instead */
} ZL_ErrorCode;

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_ERRORS_TYPES_H
