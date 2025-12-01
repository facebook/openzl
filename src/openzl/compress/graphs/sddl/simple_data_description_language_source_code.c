// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/sddl/simple_data_description_language_source_code.h"

#include <stdio.h>

void ZL_SDDL_SourceCode_init(
        Arena* const arena,
        ZL_SDDL_SourceCode* const sc,
        const StringView sv)
{
    if (sc == NULL) {
        return;
    }
    memset(sc, 0, sizeof(*sc));
    if (arena == NULL) {
        return;
    }
    // if (sv.data == NULL || sv.size == 0) {
    //     return;
    // }
    char* const source_copy = ALLOC_Arena_malloc(arena, sv.size + 1);
    if (source_copy == NULL) {
        return;
    }
    if (sv.size) {
        memcpy(source_copy, sv.data, sv.size);
    }
    source_copy[sv.size] = '\0';
    sc->source_code      = StringView_init(source_copy, sv.size);
}

void ZL_SDDL_SourceCode_initEmpty(
        Arena* const arena,
        ZL_SDDL_SourceCode* const sc)
{
    ZL_SDDL_SourceCode_init(arena, sc, StringView_init(NULL, 0));
}

void ZL_SDDL_SourceCode_destroy(
        Arena* const arena,
        ZL_SDDL_SourceCode* const sc)
{
    (void)arena;
    (void)sc;
    // Freeing the arena will take care of cleaning this up.
}

static ZL_Report ZL_SDDL_SourceLocationPrettyString_addLine(
        ZL_ErrorContext* const ctx,
        char** const bufptr,
        char* const bufend,
        const char** const srcptr,
        const char* const srcend,
        const size_t line_num,
        const int line_num_width,
        int start_col,
        int end_col,
        const int indent)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(ctx);

    const char* const src = *srcptr;

    // one past the end of the line
    const char* line_end = src;
    while (line_end < srcend && *line_end != '\n') {
        line_end++;
    }
    int line_length = (int)(line_end - src);

    int written = snprintf(
            *bufptr,
            (size_t)(bufend - *bufptr) + 1,
            "%*zu | %.*s\n",
            indent + line_num_width,
            line_num,
            line_length,
            src);
    ZL_ERR_IF_LT(written, 0, GENERIC);
    *bufptr += written;

    *srcptr = line_end;
    if (*srcptr != srcend) {
        (*srcptr)++;
    }

    if (start_col == -1) {
        start_col = 0;
    }

    if (end_col == -1) {
        end_col = line_length;
    } else if (end_col > line_length) {
        end_col = line_length;
    }

    written = snprintf(
            *bufptr,
            (size_t)(bufend - *bufptr) + 1,
            "%*s | %*s",
            indent + line_num_width,
            "",
            (int)start_col,
            "");
    ZL_ERR_IF_LT(written, 0, GENERIC);
    *bufptr += written;

    const size_t tildes = (size_t)(end_col - start_col);
    ZL_ERR_IF_GT(tildes, bufend - *bufptr, GENERIC);
    memset(*bufptr, '~', tildes);
    *bufptr += tildes;

    written = snprintf(
            *bufptr,
            (size_t)(bufend - *bufptr) + 1,
            "%*s\n",
            (int)(line_length - end_col),
            "");
    ZL_ERR_IF_LT(written, 0, GENERIC);
    *bufptr += written;

    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_SDDL_SourceLocationPrettyString)
ZL_SDDL_SourceLocationPrettyString_create(
        ZL_ErrorContext* const ctx,
        Arena* const arena,
        const ZL_SDDL_SourceCode* const sc,
        const ZL_SDDL_SourceLocation* const sl,
        const size_t _indent)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_SourceLocationPrettyString, ctx);
    (void)arena;
    ZL_SDDL_SourceLocationPrettyString pstr;

    memset(&pstr, 0, sizeof(pstr));

    ZL_ERR_IF_NULL(sc, parameter_invalid);
    const char* const sc_ptr = sc->source_code.data;
    const size_t sc_size     = sc->source_code.size;
    ZL_ERR_IF_NULL(sc_ptr, parameter_invalid);
    // ZL_ERR_IF_EQ(sc_size, 0, parameter_invalid);
    ZL_ERR_IF_NULL(sl, parameter_invalid);

    const size_t start = sl->start;
    const size_t size  = sl->size;
    ZL_ERR_IF_GT(start, sc_size, parameter_invalid);
    ZL_ERR_IF_GT(size, sc_size, parameter_invalid);
    ZL_ERR_IF_GT(start + size, sc_size, parameter_invalid);

    const int indent = (int)_indent;

    size_t start_line_num        = 1;
    const char* start_line_start = sc_ptr;

    for (size_t pos = 0; pos < start; pos++) {
        if (sc_ptr[pos] == '\n') {
            start_line_num++;
            start_line_start = &sc_ptr[pos + 1];
        }
    }

    size_t end_line_num        = start_line_num;
    const char* end_line_start = start_line_start;

    for (size_t pos = start; pos + 1 < start + size; pos++) {
        if (sc_ptr[pos] == '\n') {
            end_line_num++;
            end_line_start = &sc_ptr[pos + 1];
        }
    }

    // one past the end of the line
    const char* end_line_end = end_line_start;
    while (end_line_end < sc_ptr + sc_size && *end_line_end != '\n') {
        end_line_end++;
    }

    const int start_line_start_col = (int)(sc_ptr + start - start_line_start);
    const int end_line_end_col = (int)(sc_ptr + start + size - end_line_start);

    const size_t num_lines = (size_t)(end_line_num - start_line_num + 1);
    const size_t line_lengths_total = (size_t)(end_line_end - start_line_start);

    static const char* const pos_str_fmt =
            "%*sSDDL source code from line:col %zu:%d to %zu:%d:\n";
    const int pos_str_len = snprintf(
            NULL,
            0,
            pos_str_fmt,
            indent,
            "",
            start_line_num,
            start_line_start_col + 1,
            end_line_num,
            end_line_end_col + 1);
    ZL_ERR_IF_LE(pos_str_len, 0, GENERIC);

    const int line_num_width = snprintf(NULL, 0, "%zu", end_line_num);
    ZL_ERR_IF_LE(line_num_width, 0, GENERIC);

    const size_t cap = (size_t)pos_str_len
            + (line_lengths_total + 1
               + ((size_t)indent + (size_t)line_num_width + 3) * num_lines)
                    * 2
            + 1 /* trailing null byte */;
    char* buf = ALLOC_Arena_malloc(arena, cap);
    ZL_ERR_IF_NULL(buf, allocation);
    char* const bufend = buf + cap - 1;

    size_t line_num        = start_line_num;
    const char* line_start = start_line_start;

    pstr._buf     = buf;
    pstr.str.data = buf;

    {
        const int pos_str_actual_len = snprintf(
                buf,
                (size_t)(bufend - buf),
                pos_str_fmt,
                indent,
                "",
                start_line_num,
                start_line_start_col + 1,
                end_line_num,
                end_line_end_col + 1);
        ZL_ERR_IF_NE(pos_str_actual_len, pos_str_len, GENERIC);
        buf += pos_str_actual_len;
    }

    while (true) {
        const char* const initial_start = line_start;

        int start_col = -1;
        int end_col   = -1;

        if (line_start == start_line_start) {
            start_col = start_line_start_col;
        }
        if (line_start == end_line_start) {
            end_col = end_line_end_col;
        }

        ZL_ERR_IF_ERR(ZL_SDDL_SourceLocationPrettyString_addLine(
                ZL_ERR_CTX_PTR,
                &buf,
                bufend,
                &line_start,
                sc_ptr + sc_size,
                line_num,
                line_num_width,
                start_col,
                end_col,
                indent));
        line_num++;
        if (initial_start >= end_line_start) {
            break;
        }
    }

    pstr.str.size = (size_t)(buf - pstr.str.data);
    ZL_ERR_IF_NE(pstr.str.size + 1, cap, GENERIC);
    *buf = '\0';

    return ZL_WRAP_VALUE(pstr);
}

void ZL_SDDL_SourceLocationPrettyString_destroy(
        Arena* const arena,
        const ZL_SDDL_SourceLocationPrettyString* const pretty_str)
{
    if (pretty_str == NULL) {
        return;
    }
    ALLOC_Arena_free(arena, pretty_str->_buf);
}
