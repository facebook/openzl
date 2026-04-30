// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef TIME_FN_H_MODULE_287987
#define TIME_FN_H_MODULE_287987

#if defined(__cplusplus)
extern "C" {
#endif

/*-****************************************
 *  Types
 ******************************************/

#include <stdint.h>           // uint64_t
typedef uint64_t Duration_ns; /* Precise Time */

/* TIME_t contains a nanosecond time counter.
 * The absolute value is not meaningful.
 * It's only valid to compute Duration_ns between 2 measurements. */
typedef struct {
    Duration_ns t;
} TIME_t;
#define TIME_INITIALIZER { 0 }

/*-****************************************
 *  Time functions
 ******************************************/

TIME_t TIME_getTime(void);

/* Timer resolution can be low on some platforms.
 * To improve accuracy, it's recommended to wait for a new tick
 * before starting benchmark measurements */
void TIME_waitForNextTick(void);
/* tells if timefn will return correct time measurements
 * in presence of multi-threaded workload.
 * note : this is not the case if only C90 clock_t measurements are available */
int TIME_support_MT_measurements(void);

Duration_ns TIME_span_ns(TIME_t clockStart, TIME_t clockEnd);
Duration_ns TIME_clockSpan_ns(TIME_t clockStart);

#if defined(__cplusplus)
}
#endif

#endif /* TIME_FN_H_MODULE_287987 */
