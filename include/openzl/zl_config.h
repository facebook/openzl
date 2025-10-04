// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_ZL_CONFIG_H
#define OPENZL_ZL_CONFIG_H

// Configuration header for OpenZL library.
// These macros control optional features and optimizations.
// Values can be overridden using preprocessor variables (-D flags in CPPFLAGS)

// Ensure all configuration defines have defaults if not provided by build
// system
#ifndef ZL_ALLOW_INTROSPECTION
#    define ZL_ALLOW_INTROSPECTION 1
#endif

#ifndef ZL_HAVE_FBCODE
#    define ZL_HAVE_FBCODE 0
#endif

#ifndef ZL_HAVE_X86_64_ASM
#    define ZL_HAVE_X86_64_ASM 0
#endif

// BEGIN CODEMOD DEFINES - Legacy compatibility
#ifndef OPENZL_HAVE_FBCODE
#    define OPENZL_HAVE_FBCODE ZL_HAVE_FBCODE // TODO(T223464378): Delete
#endif
#ifndef OPENZL_HAVE_X86_64_ASM
#    define OPENZL_HAVE_X86_64_ASM \
        ZL_HAVE_X86_64_ASM // TODO(T223464378): Delete
#endif
// END CODEMOD DEFINES

#endif // OPENZL_ZL_CONFIG_H
