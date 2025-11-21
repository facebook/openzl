// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/assertion.h" // ZL_ASSERT_FAIL
#include "openzl/common/errors_internal.h"
#include "openzl/common/limits.h"
#include "openzl/common/logging.h" // ZL_VFDLOG
#include "openzl/common/operation_context.h"
#include "openzl/common/vector.h"
#include "openzl/zl_errors.h"

#include <stdarg.h> // va_list, va_start, va_end
#include <string.h> // memset

const char* ZL_ErrorCode_toString(ZL_ErrorCode code)
{
    switch (code) {
        case ZL_ErrorCode_no_error:
            return ZL_ErrorCode_no_error__desc_str;
        case ZL_ErrorCode_GENERIC:
            return ZL_ErrorCode_GENERIC__desc_str;
        case ZL_ErrorCode_allocation:
            return ZL_ErrorCode_allocation__desc_str;
        case ZL_ErrorCode_srcSize_tooSmall:
            return ZL_ErrorCode_srcSize_tooSmall__desc_str;
        case ZL_ErrorCode_dstCapacity_tooSmall:
            return ZL_ErrorCode_dstCapacity_tooSmall__desc_str;
        case ZL_ErrorCode_userBuffer_alignmentIncorrect:
            return ZL_ErrorCode_userBuffer_alignmentIncorrect__desc_str;
        case ZL_ErrorCode_userBuffers_invalidNum:
            return ZL_ErrorCode_userBuffers_invalidNum__desc_str;
        case ZL_ErrorCode_decompression_incorrectAPI:
            return ZL_ErrorCode_decompression_incorrectAPI__desc_str;
        case ZL_ErrorCode_invalidName:
            return ZL_ErrorCode_invalidName__desc_str;
        case ZL_ErrorCode_header_unknown:
            return ZL_ErrorCode_header_unknown__desc_str;
        case ZL_ErrorCode_frameParameter_unsupported:
            return ZL_ErrorCode_frameParameter_unsupported__desc_str;
        case ZL_ErrorCode_outputID_invalid:
            return ZL_ErrorCode_outputID_invalid__desc_str;
        case ZL_ErrorCode_invalidRequest_singleOutputFrameOnly:
            return ZL_ErrorCode_invalidRequest_singleOutputFrameOnly__desc_str;
        case ZL_ErrorCode_outputNotCommitted:
            return ZL_ErrorCode_outputNotCommitted__desc_str;
        case ZL_ErrorCode_outputNotReserved:
            return ZL_ErrorCode_outputNotReserved__desc_str;
        case ZL_ErrorCode_compressionParameter_invalid:
            return ZL_ErrorCode_compressionParameter_invalid__desc_str;
        case ZL_ErrorCode_segmenter_inputNotConsumed:
            return ZL_ErrorCode_segmenter_inputNotConsumed__desc_str;
        case ZL_ErrorCode_graph_invalid:
            return ZL_ErrorCode_graph_invalid__desc_str;
        case ZL_ErrorCode_graph_nonserializable:
            return ZL_ErrorCode_graph_nonserializable__desc_str;
        case ZL_ErrorCode_graph_invalidNumInputs:
            return ZL_ErrorCode_graph_invalidNumInputs__desc_str;
        case ZL_ErrorCode_successor_invalid:
            return ZL_ErrorCode_successor_invalid__desc_str;
        case ZL_ErrorCode_successor_alreadySet:
            return ZL_ErrorCode_successor_alreadySet__desc_str;
        case ZL_ErrorCode_successor_invalidNumInputs:
            return ZL_ErrorCode_successor_invalidNumInputs__desc_str;
        case ZL_ErrorCode_inputType_unsupported:
            return ZL_ErrorCode_inputType_unsupported__desc_str;
        case ZL_ErrorCode_graphParameter_invalid:
            return ZL_ErrorCode_graphParameter_invalid__desc_str;
        case ZL_ErrorCode_nodeParameter_invalid:
            return ZL_ErrorCode_nodeParameter_invalid__desc_str;
        case ZL_ErrorCode_nodeParameter_invalidValue:
            return ZL_ErrorCode_nodeParameter_invalidValue__desc_str;
        case ZL_ErrorCode_transform_executionFailure:
            return ZL_ErrorCode_transform_executionFailure__desc_str;
        case ZL_ErrorCode_customNode_definitionInvalid:
            return ZL_ErrorCode_customNode_definitionInvalid__desc_str;
        case ZL_ErrorCode_stream_wrongInit:
            return ZL_ErrorCode_stream_wrongInit__desc_str;
        case ZL_ErrorCode_streamType_incorrect:
            return ZL_ErrorCode_streamType_incorrect__desc_str;
        case ZL_ErrorCode_streamCapacity_tooSmall:
            return ZL_ErrorCode_streamCapacity_tooSmall__desc_str;
        case ZL_ErrorCode_streamParameter_invalid:
            return ZL_ErrorCode_streamParameter_invalid__desc_str;
        case ZL_ErrorCode_parameter_invalid:
            return ZL_ErrorCode_parameter_invalid__desc_str;
        case ZL_ErrorCode_formatVersion_unsupported:
            return ZL_ErrorCode_formatVersion_unsupported__desc_str;
        case ZL_ErrorCode_formatVersion_notSet:
            return ZL_ErrorCode_formatVersion_notSet__desc_str;
        case ZL_ErrorCode_node_versionMismatch:
            return ZL_ErrorCode_node_versionMismatch__desc_str;
        case ZL_ErrorCode_node_unexpected_input_type:
            return ZL_ErrorCode_node_unexpected_input_type__desc_str;
        case ZL_ErrorCode_node_invalid_input:
            return ZL_ErrorCode_node_invalid_input__desc_str;
        case ZL_ErrorCode_node_invalid:
            return ZL_ErrorCode_node_invalid__desc_str;
        case ZL_ErrorCode_nodeExecution_invalidOutputs:
            return ZL_ErrorCode_nodeExecution_invalidOutputs__desc_str;
        case ZL_ErrorCode_nodeRegen_countIncorrect:
            return ZL_ErrorCode_nodeRegen_countIncorrect__desc_str;
        case ZL_ErrorCode_logicError:
            return ZL_ErrorCode_logicError__desc_str;
        case ZL_ErrorCode_invalidTransform:
            return ZL_ErrorCode_invalidTransform__desc_str;
        case ZL_ErrorCode_internalBuffer_tooSmall:
            return ZL_ErrorCode_internalBuffer_tooSmall__desc_str;
        case ZL_ErrorCode_corruption:
            return ZL_ErrorCode_corruption__desc_str;
        case ZL_ErrorCode_outputs_tooNumerous:
            return ZL_ErrorCode_outputs_tooNumerous__desc_str;
        case ZL_ErrorCode_temporaryLibraryLimitation:
            return ZL_ErrorCode_temporaryLibraryLimitation__desc_str;
        case ZL_ErrorCode_compressedChecksumWrong:
            return ZL_ErrorCode_compressedChecksumWrong__desc_str;
        case ZL_ErrorCode_contentChecksumWrong:
            return ZL_ErrorCode_contentChecksumWrong__desc_str;
        case ZL_ErrorCode_srcSize_tooLarge:
            return ZL_ErrorCode_srcSize_tooLarge__desc_str;
        case ZL_ErrorCode_integerOverflow:
            return ZL_ErrorCode_integerOverflow__desc_str;
        case ZL_ErrorCode_maxCode:
        default:
            ZL_ASSERT_FAIL("Invalid error code!: %d", (int)code);
            return "INVALID_CODE!";
    }
}

typedef struct {
    char const* file;
    char const* func;
    int line;
    size_t messageOffset;
} ErrorFrameImpl;

DECLARE_VECTOR_TYPE(ErrorFrameImpl)

struct ZL_DynamicErrorInfo_s {
    VECTOR(char) messageBuffer;
    VECTOR(ErrorFrameImpl) stackFrames;
    ZL_ErrorCode code;
    size_t messageOffset;
    ZL_GraphContext graphContext;
    char* cachedErrorString;
};

/// Checks whether @info is empty.
ZL_INLINE bool ZL_EE_isEmpty(const ZL_ErrorInfo info)
{
    return info._st == NULL;
}

ZL_INLINE ZL_DynamicErrorInfo* ZL_EE_dy(const ZL_ErrorInfo info)
{
    const uintptr_t bits = (uintptr_t)info._dy;
    return (info._dy != NULL && ((bits & 1) == 1))
            ? (ZL_DynamicErrorInfo*)(bits & ~(uintptr_t)1)
            : NULL;
}

ZL_INLINE const ZL_StaticErrorInfo* ZL_EE_st(const ZL_ErrorInfo info)
{
    const uintptr_t bits = (uintptr_t)info._st;
    return (info._st != NULL && ((bits & 1) == 0)) ? info._st : NULL;
}

ZL_DynamicErrorInfo* ZL_E_dy(const ZL_Error err)
{
    return ZL_E_isError(err) ? ZL_EE_dy(err._info) : NULL;
}

const ZL_StaticErrorInfo* ZL_E_st(const ZL_Error err)
{
    return ZL_E_isError(err) ? ZL_EE_st(err._info) : NULL;
}

ZL_DynamicErrorInfo* ZL_DEE_create(void)
{
    ZL_DynamicErrorInfo* info = malloc(sizeof(ZL_DynamicErrorInfo));
    if (info != NULL) {
        VECTOR_INIT(info->messageBuffer, ZL_CONTAINER_SIZE_LIMIT);
        VECTOR_INIT(info->stackFrames, ZL_CONTAINER_SIZE_LIMIT);
        info->cachedErrorString = NULL;
        ZL_DEE_clear(info);
    }
    return info;
}

ZL_ErrorInfo ZL_EE_create(void)
{
    ZL_DynamicErrorInfo* dy = ZL_DEE_create();
    return ZL_EI_fromDy(dy);
}

static void ZL_DEE_clearErrorString(ZL_DynamicErrorInfo* info)
{
    if (info == NULL)
        return;
    free(info->cachedErrorString);
    info->cachedErrorString = NULL;
}

void ZL_DEE_free(ZL_DynamicErrorInfo* info)
{
    if (info == NULL)
        return;
    VECTOR_DESTROY(info->messageBuffer);
    VECTOR_DESTROY(info->stackFrames);
    free(info->cachedErrorString);
    free(info);
}

void ZL_EE_free(ZL_ErrorInfo ei)
{
    ZL_DynamicErrorInfo* info = ZL_EE_dy(ei);
    ZL_DEE_free(info);
}

void ZL_DEE_clear(ZL_DynamicErrorInfo* info)
{
    if (info == NULL)
        return;
    ZL_DEE_clearErrorString(info);
    VECTOR_CLEAR(info->messageBuffer);
    VECTOR_CLEAR(info->stackFrames);
    info->code          = ZL_ErrorCode_no_error;
    info->messageOffset = (size_t)-1;
    memset(&info->graphContext, 0, sizeof(info->graphContext));
}

void ZL_EE_clear(ZL_ErrorInfo ei)
{
    ZL_DynamicErrorInfo* info = ZL_EE_dy(ei);
    ZL_DEE_clear(info);
}

/// Prints the format string to the messageBuffer and returns the offset
/// to the printed message in the messageBuffer.
static size_t
ZL_DEE_internPrintf_va(ZL_DynamicErrorInfo* info, char const* fmt, va_list args)
{
    size_t const messageOffset = VECTOR_SIZE(info->messageBuffer);
    size_t messageEnd          = VECTOR_CAPACITY(info->messageBuffer);
    size_t messageCapacity     = messageEnd - messageOffset;

    // First try to write the message into the buffer without resizing.
    // This allows us to succeed if it is large enough, and tells us how
    // much space we need if it fails.
    va_list argsCopy;
    va_copy(argsCopy, args);
    int const messageSize = vsnprintf(
            messageCapacity ? VECTOR_DATA(info->messageBuffer) + messageOffset
                            : NULL,
            messageCapacity,
            fmt,
            argsCopy);
    va_end(argsCopy);
    if (messageSize < 0)
        return (size_t)-1;
    ZL_ASSERT_GE(messageSize, 0);

    if ((size_t)messageSize >= messageCapacity) {
        // If we didn't have enough space, increase the buffer size & try again.
        size_t const allocSize = messageOffset + (size_t)messageSize + 1;
        if (VECTOR_RESERVE(info->messageBuffer, allocSize) < allocSize)
            return (size_t)-1;
        messageEnd      = VECTOR_CAPACITY(info->messageBuffer);
        messageCapacity = messageEnd - messageOffset;
        ZL_ASSERT_LT((size_t)messageSize, messageCapacity);

        int const writtenSize = vsnprintf(
                VECTOR_DATA(info->messageBuffer) + messageOffset,
                messageCapacity,
                fmt,
                args);
        ZL_ASSERT_EQ(writtenSize, messageSize);
    }

    // Resize to include the message we wrote.
    (void)VECTOR_RESIZE_UNINITIALIZED(
            info->messageBuffer, messageOffset + (size_t)messageSize + 1);

    return messageOffset;
}

static size_t
ZL_DEE_internPrintf(ZL_DynamicErrorInfo* info, char const* fmt, ...)
{
    if (info == NULL)
        return (size_t)-1;
    va_list args;
    va_start(args, fmt);
    size_t const messageOffset = ZL_DEE_internPrintf_va(info, fmt, args);
    va_end(args);
    return messageOffset;
}

static void ZL_DEE_addFrame(
        ZL_DynamicErrorInfo* info,
        ZL_ErrorContext const* scopeCtx,
        char const* file,
        char const* func,
        int line,
        size_t messageOffset)
{
    if (info == NULL)
        return;
    ZL_DEE_clearErrorString(info);
    // Attach the graph context if not already present
    if (scopeCtx) {
        if (info->graphContext.nodeID.nid == 0)
            info->graphContext.nodeID = scopeCtx->graphCtx.nodeID;
        if (info->graphContext.graphID.gid == 0)
            info->graphContext.graphID = scopeCtx->graphCtx.graphID;
        if (info->graphContext.transformID == 0)
            info->graphContext.transformID = scopeCtx->graphCtx.transformID;
        if (info->graphContext.name == NULL)
            info->graphContext.name = scopeCtx->graphCtx.name;
    }

    // Set the message if not already set
    if (info->messageOffset == (size_t)-1) {
        info->messageOffset = messageOffset;
    }

    // Add the stack frame
    ErrorFrameImpl const frame = {
        .file = file, .func = func, .line = line, .messageOffset = messageOffset
    };
    // Might fail, but fail silently
    (void)VECTOR_PUSHBACK(info->stackFrames, frame);
}

static void ZL_DEE_fill_va(
        ZL_DynamicErrorInfo* info,
        ZL_ErrorContext const* scopeCtx,
        char const* file,
        char const* func,
        int line,
        ZL_ErrorCode code,
        char const* fmt,
        va_list args)
{
    if (info == NULL)
        return;
    ZL_DEE_clearErrorString(info);
    info->code = code;

    size_t const messageOffset = ZL_DEE_internPrintf_va(info, fmt, args);
    if (messageOffset == (size_t)-1)
        return;
    ZL_DEE_addFrame(info, scopeCtx, file, func, line, messageOffset);
}

static void ZL_DEE_fill(
        ZL_DynamicErrorInfo* info,
        ZL_ErrorContext const* scopeCtx,
        char const* file,
        char const* func,
        int line,
        ZL_ErrorCode code,
        char const* fmt,
        ...)
{
    va_list args;
    va_start(args, fmt);
    ZL_DEE_fill_va(info, scopeCtx, file, func, line, code, fmt, args);
    va_end(args);
}

static void ZL_DEE_appendToMessage_va(
        ZL_DynamicErrorInfo* info,
        char const* fmt,
        va_list args)
{
    if (info == NULL || info->messageOffset == (size_t)-1)
        return;
    ZL_DEE_clearErrorString(info);

    // Remove the trailing '\0'
    size_t const size = VECTOR_SIZE(info->messageBuffer);
    ZL_ASSERT_NE(size, 0);
    ZL_ASSERT_EQ(VECTOR_AT(info->messageBuffer, size - 1), '\0');
    VECTOR_POPBACK(info->messageBuffer);

    // Append to the previous message
    ZL_DEE_internPrintf_va(info, fmt, args);
}

static ZL_ErrorCode ZL_SEE_code(ZL_StaticErrorInfo const* info)
{
    return info ? info->code : ZL_ErrorCode_no_error;
}

ZL_ErrorCode ZL_DEE_code(ZL_DynamicErrorInfo const* info)
{
    return info ? info->code : ZL_ErrorCode_no_error;
}

ZL_ErrorCode ZL_EE_code(ZL_ErrorInfo ei)
{
    ZL_StaticErrorInfo const* const st = ZL_EE_st(ei);
    if (st != NULL) {
        return ZL_SEE_code(st);
    }
    ZL_DynamicErrorInfo const* const dy = ZL_EE_dy(ei);
    if (dy != NULL) {
        return ZL_DEE_code(dy);
    }
    return ZL_ErrorCode_no_error;
}

static char const* ZL_SEE_message(ZL_StaticErrorInfo const* info)
{
    if (info == NULL)
        return NULL;
    return info->fmt;
}

static char const* ZL_DEE_message(ZL_DynamicErrorInfo const* info)
{
    if (info == NULL || info->messageOffset == (size_t)-1)
        return NULL;
    return VECTOR_DATA(info->messageBuffer) + info->messageOffset;
}

char const* ZL_EE_message(ZL_ErrorInfo ei)
{
    ZL_StaticErrorInfo const* const st = ZL_EE_st(ei);
    if (st != NULL) {
        return ZL_SEE_message(st);
    }
    ZL_DynamicErrorInfo const* const dy = ZL_EE_dy(ei);
    if (dy != NULL) {
        return ZL_DEE_message(dy);
    }
    return NULL;
}

static size_t ZL_DEE_nbStackFrames(ZL_DynamicErrorInfo const* info)
{
    return info ? VECTOR_SIZE(info->stackFrames) : 0;
}

size_t ZL_EE_nbStackFrames(ZL_ErrorInfo ei)
{
    ZL_StaticErrorInfo const* const st = ZL_EE_st(ei);
    if (st != NULL) {
        return 1;
    }
    ZL_DynamicErrorInfo const* const dy = ZL_EE_dy(ei);
    if (dy != NULL) {
        return ZL_DEE_nbStackFrames(dy);
    }
    return 0;
}

static ZL_ErrorFrame ZL_DEE_stackFrame(
        ZL_DynamicErrorInfo const* const info,
        size_t const idx)
{
    ZL_ASSERT_NN(info);
    ZL_ASSERT_LT(idx, VECTOR_SIZE(info->stackFrames));
    ErrorFrameImpl const impl = VECTOR_AT(info->stackFrames, idx);
    char const* const message = impl.messageOffset != (size_t)-1
            ? VECTOR_DATA(info->messageBuffer) + impl.messageOffset
            : NULL;
    ZL_ErrorFrame const frame = {
        .file    = impl.file,
        .func    = impl.func,
        .line    = impl.line,
        .message = message,
    };
    return frame;
}

static ZL_ErrorFrame ZL_SEE_stackFrame(
        ZL_StaticErrorInfo const* const info,
        size_t const idx)
{
    ZL_ASSERT_NN(info);
    ZL_ASSERT_EQ(idx, 0);
    ZL_ErrorFrame const frame = {
        .file    = info->file,
        .func    = info->func,
        .line    = info->line,
        .message = info->fmt,
    };
    return frame;
}

ZL_ErrorFrame ZL_EE_stackFrame(ZL_ErrorInfo ei, size_t idx)
{
    ZL_StaticErrorInfo const* const st  = ZL_EE_st(ei);
    ZL_DynamicErrorInfo const* const dy = ZL_EE_dy(ei);
    ZL_ASSERT(st || dy, "Shouldn't be called on an empty error info.");
    if (st != NULL) {
        return ZL_SEE_stackFrame(st, idx);
    }
    if (dy != NULL) {
        return ZL_DEE_stackFrame(dy, idx);
    }
    return (ZL_ErrorFrame){ 0 };
}

static ZL_GraphContext ZL_DEE_graphContext(ZL_DynamicErrorInfo const* info)
{
    return info ? info->graphContext : (ZL_GraphContext){ { 0 }, { 0 }, 0, "" };
}

ZL_GraphContext ZL_EE_graphContext(ZL_ErrorInfo ei)
{
    ZL_DynamicErrorInfo const* info = ZL_EE_dy(ei);
    return ZL_DEE_graphContext(info);
}

static char const* ZL_SEE_str(const ZL_StaticErrorInfo* info)
{
    if (info == NULL)
        return "";
    if (info->fmt == NULL)
        return "";
    return info->fmt;
}

static char* ZL_DEE_stackStr(ZL_DynamicErrorInfo const* info)
{
    size_t sizeBound      = 0;
    size_t const nbFrames = VECTOR_SIZE(info->stackFrames);
    for (size_t i = 0; i < nbFrames; ++i) {
        ErrorFrameImpl const frame = VECTOR_AT(info->stackFrames, i);
        if (frame.file != NULL) {
            sizeBound += strlen(frame.file);
        } else {
            sizeBound += 3; // "???"
        }
        if (frame.func != NULL) {
            sizeBound += strlen(frame.func);
        } else {
            sizeBound += 3; // "???"
        }
        if (frame.messageOffset != (size_t)-1)
            sizeBound += strlen(
                    VECTOR_DATA(info->messageBuffer) + frame.messageOffset);
        // frame number + Line number + extra characters
        sizeBound += 32;
    }

    char* stackStr = malloc(sizeBound + 1);
    if (stackStr == NULL)
        return "";

    size_t stackStrSize = 0;
    for (size_t i = 0; i < nbFrames; ++i) {
        ErrorFrameImpl const frame = VECTOR_AT(info->stackFrames, i);
        char const* message        = "";
        if (frame.messageOffset != (size_t)-1) {
            message = VECTOR_DATA(info->messageBuffer) + frame.messageOffset;
        }
        int const frameSize = snprintf(
                stackStr + stackStrSize,
                sizeBound - stackStrSize,
                "\t#%u %s (%s:%d): %s\n",
                (unsigned)i,
                frame.func ? frame.func : "???",
                frame.file ? frame.file : "???",
                frame.line,
                message);

        // Our bound should be large enough, so assert it, but still break if it
        // isn't so we don't write out of bounds.
        ZL_ASSERT_LT((size_t)frameSize, sizeBound - stackStrSize);
        if (frameSize < 0 || (size_t)frameSize >= sizeBound - stackStrSize) {
            break;
        }

        stackStrSize += (size_t)frameSize;
    }
    ZL_ASSERT_LE(stackStrSize, sizeBound);
    stackStr[stackStrSize] = '\0';

    return stackStr;
}

/**
 * If @p str is not null, fills it in. If @p str is null, pretends to, and
 * returns the size @p str should be allocated with, so this can be reinvoked
 * with a correctly sized buffer.
 */
static size_t ZL_DEE_str_inner(
        ZL_DynamicErrorInfo* info,
        char* str,
        size_t cap,
        const char* stackStr)
{
    size_t size = 0;
    int written = snprintf(
            str != NULL ? str + size : str,
            str != NULL ? (size_t)(cap - size) : 0,
            "Code: %s\n"
            "Message: %s\n",
            ZL_ErrorCode_toString(info->code),
            ZL_DEE_message(info));
    if (written < 0) {
        return 0;
    }
    size += (size_t)written;
    if (info->graphContext.graphID.gid != 0) {
        written = snprintf(
                str != NULL ? str + size : str,
                str != NULL ? (size_t)(cap - size) : 0,
                "Graph ID: %u\n",
                info->graphContext.graphID.gid);
        if (written < 0) {
            return 0;
        }
        size += (size_t)written;
    }
    if (info->graphContext.name != NULL) {
        written = snprintf(
                str != NULL ? str + size : str,
                str != NULL ? (size_t)(cap - size) : 0,
                "Node name: %s\n",
                info->graphContext.name);
        if (written < 0) {
            return 0;
        }
        size += (size_t)written;
    }
    if (info->graphContext.nodeID.nid != 0) {
        written = snprintf(
                str != NULL ? str + size : str,
                str != NULL ? (size_t)(cap - size) : 0,
                "Node ID: %u\n",
                info->graphContext.nodeID.nid);
        if (written < 0) {
            return 0;
        }
        size += (size_t)written;
    }
    if (info->graphContext.transformID != 0) {
        written = snprintf(
                str != NULL ? str + size : str,
                str != NULL ? (size_t)(cap - size) : 0,
                "Transform ID: %u\n",
                info->graphContext.transformID);
        if (written < 0) {
            return 0;
        }
        size += (size_t)written;
    }
    written = snprintf(
            str != NULL ? str + size : str,
            str != NULL ? (size_t)(cap - size) : 0,
            "Stack Trace:\n"
            "%s",
            stackStr);
    if (written < 0) {
        return 0;
    }
    size += (size_t)written;
    return size + 1;
}

static char const* ZL_DEE_str(ZL_DynamicErrorInfo* info)
{
    if (info == NULL)
        return "";
    if (info->cachedErrorString != NULL)
        return info->cachedErrorString;

    char* str      = NULL;
    char* stackStr = ZL_DEE_stackStr(info);

    const size_t strCapacity = ZL_DEE_str_inner(info, NULL, 0, stackStr);
    if (strCapacity) {
        str = malloc(strCapacity);
        if (str != NULL) {
            const size_t written =
                    ZL_DEE_str_inner(info, str, strCapacity, stackStr);
            if (written == 0) {
                free(str);
                str = NULL;
            } else {
                ZL_ASSERT_EQ(written, strCapacity);
            }
        }
    }

    free(stackStr);
    info->cachedErrorString = str;
    return str;
}

char const* ZL_EE_str(ZL_ErrorInfo ei)
{
    ZL_StaticErrorInfo const* const st = ZL_EE_st(ei);
    if (st != NULL) {
        return ZL_SEE_str(st);
    }
    ZL_DynamicErrorInfo* const dy = ZL_EE_dy(ei);
    if (dy != NULL) {
        return ZL_DEE_str(dy);
    }
    return "";
}

void ZL_EE_log(ZL_ErrorInfo ei, int level)
{
    if (level <= ZL_g_logLevel) {
        ZL_RLOG(ALWAYS, "%s", ZL_EE_str(ei));
    }
}

void ZL_E_log(ZL_Error err, int level)
{
    if (level <= ZL_g_logLevel) {
        ZL_RLOG(ALWAYS, "%s", ZL_E_str(err));
    }
}

void ZL_E_print(ZL_Error err)
{
    ZL_E_log(err, ZL_LOG_LVL_ALWAYS);
}

char const* ZL_E_str(ZL_Error err)
{
    if (ZL_EE_isEmpty(err._info))
        return ZL_ErrorCode_toString(err._code);
    return ZL_EE_str(err._info);
}

void ZL_E_clearInfo(ZL_Error* err)
{
    if (err != NULL)
        err->_info = ZL_EE_EMPTY;
}

static ZL_Error ZL_E_create_va(
        ZL_StaticErrorInfo const* st,
        ZL_ErrorContext const* scopeCtx,
        const char* file,
        const char* func,
        int line,
        ZL_ErrorCode code,
        const char* fmt,
        va_list args)
{
    if (ZL_LOG_LVL_V <= ZL_g_logLevel) {
        va_list args_copy;
        va_copy(args_copy, args);
        ZL_FLOG(V,
                file,
                func,
                line,
                "Error created with code %d (%s):",
                code,
                ZL_ErrorCode_toString(code));
        ZL_VFRLOG(V, file, func, line, fmt, args_copy);
        ZL_FRLOG(V, file, func, line, "\n");
        va_end(args_copy);
    }

    ZL_DynamicErrorInfo* dy = (scopeCtx != NULL && scopeCtx->opCtx != NULL)
            ? ZL_OC_setError(scopeCtx->opCtx)
            : NULL;
#if ZL_ERROR_ENABLE_LEAKY_ALLOCATIONS
    if (dy == NULL) {
        dy = ZL_DEE_create();
    }
#endif
    ZL_Error error;
    if (dy != NULL) {
        ZL_DEE_fill_va(dy, scopeCtx, file, func, line, code, fmt, args);
        error = (ZL_Error){ ._code = code, ._info = ZL_EI_fromDy(dy) };
    } else {
        error = (ZL_Error){ ._code = code, ._info = ZL_EI_fromSt(st) };
    }

    // Logic errors should never actually be generated.
    ZL_ASSERT_NE(
            ZL_E_code(error),
            ZL_ErrorCode_logicError,
            "Logic error in: %s",
            ZL_E_str(error));

    return error;
}

ZL_Error ZL_E_create(
        ZL_StaticErrorInfo const* st,
        ZL_ErrorContext const* scopeCtx,
        const char* file,
        const char* func,
        int line,
        ZL_ErrorCode code,
        const char* fmt,
        ...)
{
    va_list args;
    va_start(args, fmt);
    ZL_Error error =
            ZL_E_create_va(st, scopeCtx, file, func, line, code, fmt, args);
    va_end(args);
    return error;
}

ZL_Error ZL_E_convertToDynamic(ZL_OperationContext* opCtx, ZL_Error err)
{
    if (opCtx == NULL) {
        return err;
    }
    if (!ZL_E_isError(err)) {
        return err;
    }
    if (ZL_E_dy(err) != NULL) {
        return err;
    }
    ZL_DynamicErrorInfo* dy = ZL_OC_setError(opCtx);
    if (dy == NULL) {
        return err;
    }
    dy->code                     = err._code;
    const ZL_StaticErrorInfo* st = ZL_E_st(err);
    if (st != NULL) {
        ZL_ErrorContext scopeCtx = { .opCtx = opCtx };
        ZL_ASSERT_EQ(st->code, err._code);
        ZL_DEE_fill(
                dy,
                &scopeCtx,
                st->file,
                st->func,
                st->line,
                st->code,
                "Converting static error: %s",
                st->fmt);
    }
    err._info = ZL_EI_fromDy(dy);
    return err;
}

#if ZL_ERROR_ENABLE_STACKS
static ZL_Error ZL_E_addFrame_va(
        ZL_ErrorContext const* scopeCtx,
        ZL_Error e,
        const ZL_ErrorInfo backup,
        const char* file,
        const char* func,
        int line,
        const char* fmt,
        va_list args)
{
    const ZL_ErrorCode code      = e._code;
    const ZL_StaticErrorInfo* st = ZL_E_st(e);
    ZL_DynamicErrorInfo* dy      = ZL_E_dy(e);
    if (dy == NULL && scopeCtx != NULL) {
        // Existing error is missing context, add it.
        dy      = ZL_OC_setError(scopeCtx->opCtx);
        e._info = ZL_EI_fromDy(dy);
        if (st == NULL) {
            ZL_DEE_fill(
                    dy,
                    scopeCtx,
                    file,
                    func,
                    line,
                    code,
                    "Attaching to pre-existing error: ");
            ZL_DEE_appendToMessage_va(dy, fmt, args);
        } else {
            if (st->code != ZL_ErrorCode_GENERIC) {
                ZL_ASSERT_EQ(st->code, e._code);
            }
            ZL_DEE_fill(
                    dy,
                    scopeCtx,
                    st->file,
                    st->func,
                    st->line,
                    code,
                    "Converting static error: %s",
                    st->fmt);
            size_t const messageOffset =
                    ZL_DEE_internPrintf(dy, "Forwarding error: ");
            ZL_DEE_appendToMessage_va(dy, fmt, args);
            ZL_DEE_addFrame(dy, scopeCtx, file, func, line, messageOffset);
        }
    } else if (dy != NULL) {
        // Add the stack frame to an existing error.
        size_t const messageOffset =
                ZL_DEE_internPrintf(dy, "Forwarding error: ");
        ZL_DEE_appendToMessage_va(dy, fmt, args);
        ZL_DEE_addFrame(dy, scopeCtx, file, func, line, messageOffset);
    } else if (dy == NULL && st == NULL && scopeCtx == NULL) {
        const ZL_ErrorCode backupCode = ZL_EE_code(backup);
        if (backupCode != ZL_ErrorCode_no_error
            && backupCode != ZL_ErrorCode_GENERIC) {
            /* We store the error code in two places: in the error object
             * that's passed up the stack, so we have it if there is no
             * rich error info struct available, and we also store it in
             * the rich error info (if present) so that we have it if we
             * turn the error into a warning, e.g., and want to later
             * regenerate the local error object.
             *
             * Normally the values in these two locations must match.
             * When an error info is created at the same time as the error,
             * obviously they do. When a dynamic error info is created and
             * attached later, they do because we fill it in from the code
             * in the base error. But in the new and specific case where
             * the error starts empty and a static error info is added, we
             * can't change the static error info to match. In this case,
             * we have to be willing to let them differ. We do validate
             * that the static error info is populated specifically with
             * the GENERIC error though.
             */
            ZL_ASSERT_EQ(backupCode, code);
        }
        e._info = backup;
    }

    // Logic errors should never actually be generated.
    ZL_ASSERT_NE(
            ZL_E_code(e),
            ZL_ErrorCode_logicError,
            "Logic error in: %s",
            ZL_E_str(e));

    return e;
}

void ZL_E_addFrame(
        ZL_ErrorContext const* scopeCtx,
        ZL_Error* e,
        const ZL_ErrorInfo backup,
        const char* file,
        const char* func,
        int line,
        const char* fmt,
        ...)
{
    if (e == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    *e = ZL_E_addFrame_va(scopeCtx, *e, backup, file, func, line, fmt, args);
    va_end(args);
}

ZL_Error ZL_E_addFrame_public(
        ZL_ErrorContext const* scopeCtx,
        ZL_Error e,
        const char* file,
        const char* func,
        int line,
        const char* fmt,
        ...)
{
    if (!ZL_E_isError(e)) {
        return e;
    }
    va_list args;
    va_start(args, fmt);
    e = ZL_E_addFrame_va(scopeCtx, e, ZL_EE_EMPTY, file, func, line, fmt, args);
    va_end(args);
    return e;
}
#else  // !ZL_ERROR_ENABLE_STACKS
ZL_Error ZL_E_addFrame_public(
        ZL_ErrorContext const*,
        ZL_Error e,
        const ZL_ErrorInfo,
        const char*,
        const char*,
        int,
        const char*,
        ...)
{
    return e;
}
#endif // ZL_ERROR_ENABLE_STACKS

void ZL_E_changeErrorCode(ZL_Error* e, ZL_ErrorCode code)
{
    e->_code                = code;
    ZL_DynamicErrorInfo* dy = ZL_E_dy(*e);
    if (dy != NULL) {
        dy->code = code;
    }
}

void ZL_E_appendToMessage(ZL_Error err, const char* fmt, ...)
{
    ZL_DynamicErrorInfo* info = ZL_E_dy(err);
    if (info == NULL)
        return;
    va_list args;
    va_start(args, fmt);
    ZL_DEE_appendToMessage_va(info, fmt, args);
    va_end(args);
}

ZL_Report ZL_reportError(
        const char* file,
        const char* func,
        const int line,
        const ZL_ErrorCode err,
        const char* fmt,
        ...)
{
    va_list args;
    va_start(args, fmt);
    ZL_Error error =
            ZL_E_create_va(NULL, NULL, file, func, line, err, fmt, args);
    va_end(args);
    return ZL_RESULT_WRAP_ERROR(size_t, error);
}

ZL_Report ZL_returnError(ZL_ErrorCode err)
{
    // TODO : should control that err is within bounds
    return ZL_REPORT_ERROR_CODE(err);
}
