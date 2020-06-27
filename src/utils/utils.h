#pragma once

#include <malloc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIMIT(x, min, max)                                                                    \
    ({                                                                                        \
        typeof( x ) _x = x;                                                                    \
        typeof( min ) _min = min;                                                            \
        typeof( max ) _max = max;                                                            \
        ( ( ( _x ) < ( _min ) ) ? ( _min ) : ( ( _x ) > ( _max ) ) ? ( _max) : ( _x ) );    \
})

#define DegToRad(a)   ( (a) *  0.01745329252f )
#define RadToDeg(a) ( (a) * 57.29577951f )

#define ALIGN4(x)           (((x) + 3) & ~3)
#define ALIGN32(x) (((x) + 31) & ~31)

// those work only in powers of 2
#define ROUNDDOWN(val, align)   ((val) & ~(align-1))
#define ROUNDUP(val, align)     ROUNDDOWN(((val) + (align-1)), align)


#define le16(i)         ((((uint16_t) ((i) & 0xFF)) << 8) | ((uint16_t) (((i) & 0xFF00) >> 8)))
#define le32(i)         ((((uint32_t)le16((i) & 0xFFFF)) << 16) | ((uint32_t)le16(((i) & 0xFFFF0000) >> 16)))
#define le64(i) ((((uint64_t)le32((i) & 0xFFFFFFFFLL)) << 32) | ((uint64_t)le32(((i) & 0xFFFFFFFF00000000LL) >> 32)))

//Needs to have log_init() called beforehand.
void dumpHex(const void *data, size_t size);

#ifdef __cplusplus
}
#endif