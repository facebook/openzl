// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/py/pybind_helpers.h"

#include <assert.h>
#include <cstddef>
#include <stdexcept>

#include <fmt/format.h>
#include <pybind11/stl.h>

#include "openzl/common/assertion.h"
#include "openzl/common/stream.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_input.h"

namespace zstrong::pybind {

std::optional<size_t> getNativeIntegerSize(std::string_view format)
{
    if (format.size() == 0) {
        throw std::runtime_error{ "Invalid format string!" };
    }

    if (format[0] == '@') {
        format = format.substr(1);
    }

    if (format.size() != 1) {
        return std::nullopt;
    }

    switch (format[0]) {
        case 'c':
            return sizeof(char);
        case 'b':
            return sizeof(signed char);
        case 'B':
            return sizeof(unsigned char);
        case 'h':
            return sizeof(short);
        case 'H':
            return sizeof(unsigned short);
        case 'i':
            return sizeof(int);
        case 'I':
            return sizeof(unsigned int);
        case 'l':
            return sizeof(long);
        case 'L':
            return sizeof(unsigned long);
        case 'q':
            return sizeof(long long);
        case 'Q':
            return sizeof(unsigned long long);
        case 'n':
            return sizeof(ssize_t);
        case 'N':
            return sizeof(size_t);
        default:
            return std::nullopt;
    }
}

bool bufferIsContiguousCStrides(py::buffer_info const& info)
{
    auto const ndim = info.shape.size();
    assert(info.strides.size() == info.shape.size());
    if (ndim == 0)
        return true;
    // The final stride must be equal to the itemsize.
    if (info.strides[ndim - 1] != info.itemsize)
        return false;
    for (size_t i = ndim - 1; i > 0; --i) {
        // Each previous stride must be the current stride * shape.
        // The current stride is the size of the scalar/array at the
        // current index. The shape is the number of elements we have
        // at the current index.
        if (info.strides[i - 1] != info.shape[i] * info.strides[i])
            return false;
    }
    return true;
}

/// Converts the input into a numpy array for Python consumption.
/// This performs a copy because the Python code may keep a reference to the
/// stream. We may be able to optimize this in the future.
py::array
toNumpyArray(ZL_Type type, size_t nbElts, size_t eltWidth, void const* ptr)
{
    switch (type) {
        case ZL_Type_string:
            throw std::runtime_error{
                "Variable size fields are not supported"
            };
        case ZL_Type_numeric:
            if (eltWidth == 1) {
                return py::array_t<uint8_t>(
                        (ssize_t)nbElts, (uint8_t const*)ptr);
            }
            if (eltWidth == 2) {
                return py::array_t<uint16_t>(
                        (ssize_t)nbElts, (uint16_t const*)ptr);
            }
            if (eltWidth == 4) {
                return py::array_t<uint32_t>(
                        (ssize_t)nbElts, (uint32_t const*)ptr);
            }
            ZL_ASSERT_EQ(eltWidth, 8);
            return py::array_t<uint64_t>((ssize_t)nbElts, (uint64_t const*)ptr);
        case ZL_Type_serial:
            return py::array_t<uint8_t>((ssize_t)nbElts, (uint8_t const*)ptr);
        case ZL_Type_struct:
            return py::array_t<uint8_t>(
                    { nbElts, eltWidth }, (uint8_t const*)ptr);
        default:
            throw std::runtime_error{ "Unknown stream type!" };
    }
}

py::array toNumpyArray(ZL_Input const* stream)
{
    return toNumpyArray(
            ZL_Input_type(stream),
            ZL_Input_numElts(stream),
            ZL_Input_eltWidth(stream),
            ZL_Input_ptr(stream));
}

py::list toList(ZL_Input const* stream)
{
    if (ZL_Input_type(stream) != ZL_Type_string) {
        throw std::runtime_error{
            "Non variable_size_fields must use numpy arrays"
        };
    }

    size_t const nbElts          = ZL_Input_numElts(stream);
    auto const* const fieldSizes = ZL_Input_stringLens(stream);
    char const* content          = (char const*)ZL_Input_ptr(stream);

    py::list pyList;
    for (size_t i = 0; i < nbElts; ++i) {
        auto const fieldSize = fieldSizes[i];
        pyList.append(py::bytes(std::string{ content, content + fieldSize }));
        content += fieldSize;
    }

    return pyList;
}

ZL_Output* bufferToStream(
        py::buffer_info const& buffer,
        ZL_Type streamType,
        const std::function<ZL_Output*(size_t nbElts_, size_t eltWidth_)>&
                createStreamFn)
{
    // Validate that we have the expected number of dimensions.
    if (streamType == ZL_Type_serial || streamType == ZL_Type_numeric) {
        if (buffer.ndim != 1) {
            throw std::runtime_error(
                    "Serial & numeric buffers must be one dimensional");
        }
    } else {
        ZL_ASSERT_EQ((int)streamType, (int)ZL_Type_struct);
        if (buffer.ndim != 2) {
            throw std::runtime_error(
                    "Fixed size field buffers must be two dimensional");
        }
    }
    if (streamType == ZL_Type_string) {
        throw std::runtime_error{ "Variable size fields not supported" };
    }

    size_t nbElts;
    size_t eltWidth;

    if (streamType == ZL_Type_serial || streamType == ZL_Type_struct) {
        if (buffer.itemsize != 1) {
            throw std::runtime_error(
                    "Serial & fixed size field buffers must have itemsize=1");
        }
        if (buffer.format != py::format_descriptor<uint8_t>::format()) {
            throw std::runtime_error(
                    "Serial & fixed size field buffers must be bytes.");
        }
        nbElts   = (size_t)buffer.shape[0];
        eltWidth = buffer.shape.size() == 2 ? (size_t)buffer.shape[1] : 1;
    } else {
        // Only accept integer formats, disallow other formats like
        // floats/doubles, because that could easily introduce loss.
        // Users can work around this by casting their arrays to integers if
        // they really want to work with a different type.
        ZL_ASSERT(streamType == ZL_Type_numeric);
        auto expectedItemSize = pybind::getNativeIntegerSize(buffer.format);
        if (!expectedItemSize.has_value()) {
            throw std::runtime_error(
                    fmt::format(
                            "numeric stream has unexpected format {} (itemsize={})",
                            buffer.format,
                            (unsigned)buffer.itemsize));
        }
        if ((size_t)buffer.itemsize != *expectedItemSize) {
            throw std::runtime_error(
                    fmt::format(
                            "Unexpected item size for format {}",
                            buffer.format));
        }

        nbElts   = (size_t)buffer.shape[0];
        eltWidth = (size_t)buffer.itemsize;
    }

    auto stream = createStreamFn(nbElts, eltWidth);
    if (!stream) {
        return nullptr;
    }
    ZL_ASSERT_EQ(ZL_validResult(ZL_Output_eltWidth(stream)), eltWidth);
    auto streamBuffer = ZL_Output_ptr(stream);

    if (bufferIsContiguousCStrides(buffer)) {
        memcpy(streamBuffer, buffer.ptr, nbElts * eltWidth);
    } else {
        ZL_ASSERT(
                (size_t)buffer.strides[0] != eltWidth
                || (buffer.shape.size() == 2 && buffer.strides[1] != 1));
        // The buffer isn't contiguous, copy it into a denseArray so
        // that it is contiguous. We could avoid this copy, but this
        // simplifies the code a lot.
        auto const denseArray = py::array(buffer);
        ZL_ASSERT(denseArray.owndata());
        ZL_ASSERT(pybind::bufferIsContiguousCStrides(denseArray.request()));
        ZL_ASSERT((size_t)denseArray.strides()[0] == eltWidth);
        ZL_ASSERT(buffer.shape.size() == 1 || denseArray.strides()[1] == 1);
        memcpy(streamBuffer, denseArray.data(), nbElts * eltWidth);
    }
    if (ZL_isError(ZL_Output_commit(stream, nbElts))) {
        throw std::runtime_error{ "Failed to commit Zstrong stream" };
    }
    return stream;
}

ZL_Output* arrayToStream(
        py::array const& array,
        ZL_Type streamType,
        const std::function<ZL_Output*(size_t nbElts, size_t eltWidth)>&
                createStreamFn)
{
    return bufferToStream(array.request(), streamType, createStreamFn);
}

ZL_Output* arrayToStream(py::array const& array, ZL_Type streamType)
{
    return arrayToStream(
            array, streamType, [streamType](auto nbElts, auto eltWidth) {
                auto s = STREAM_create(ZL_DATA_ID_INPUTSTREAM);
                if (!s) {
                    throw std::runtime_error("Failed allocating stream");
                }
                auto r = STREAM_reserve(s, streamType, eltWidth, nbElts);
                if (ZL_isError(r)) {
                    STREAM_free(s);
                    throw std::runtime_error("Error reserving ZL_Data");
                }
                return ZL_codemodDataAsOutput(s);
            });
}

ZL_Output* listToStream(
        py::list const& list,
        ZL_Type streamType,
        const std::function<ZL_Output*(size_t contentSize)>& createStreamFn)
{
    if (streamType != ZL_Type_string) {
        throw std::runtime_error{
            "Non variable_size_fields must return numpy arrays"
        };
    }
    auto data          = list.cast<std::vector<std::string>>();
    size_t contentSize = 0;
    for (auto const& x : data) {
        contentSize += x.size();
    }
    auto stream = createStreamFn(contentSize);
    uint32_t* const fieldSizes =
            ZL_Output_reserveStringLens(stream, data.size());
    if (fieldSizes == nullptr) {
        throw std::bad_alloc{};
    }
    char* content = (char*)ZL_Output_ptr(stream);
    for (size_t i = 0; i < data.size(); ++i) {
        fieldSizes[i] = (uint32_t)data[i].size();
        memcpy(content, data[i].data(), data[i].size());
        content += data[i].size();
    }
    if (ZL_isError(ZL_Output_commit(stream, data.size()))) {
        throw std::runtime_error{ "Failed to commit Zstrong stream" };
    }
    return stream;
}

} // namespace zstrong::pybind
