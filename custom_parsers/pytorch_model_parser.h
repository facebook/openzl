// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_PYTORCH_MODEL_PARSER_H
#define CUSTOM_PARSERS_PYTORCH_MODEL_PARSER_H

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

/**
 * This graph compresses Zip files containing PyTorch models.
 * It lexes the Zip file using the ZL_ZipLexer, and searches for files with
 * a `data/` or `xl_model_weights/` in their path that aren't already
 * compressed. The floating point format for each of these files is detected,
 * and the file is compressed using the appropriate float compressor.
 * All other files are either stored if they are already compressed, or
 * compressed with Zstandard.
 *
 * @warning This graph will fail to compress if the input is not a valid Zip
 * file. Or if the entries in the Zip file central directory is not in order of
 * occurrence (unlikely).
 */
ZL_GraphID ZL_createGraph_pytorchModelCompressor(ZL_Compressor* cgraph);

ZL_END_C_DECLS

#endif
