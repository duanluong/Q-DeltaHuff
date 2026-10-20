/**
 * @file Types.h
 * @brief Minimal host-side stub of the firmware's src/OS/Common/Inc/Types.h,
 * providing only the subset of types/macros used by Huffman_Decode.h/.c.
 *
 * @note This stub intentionally avoids pulling in the real Types.h (and its
 * transitive dependency on config.h / hardware-specific macros), so that the
 * Huffman_Decode module can be compiled and unit-tested standalone with a
 * host gcc toolchain. Keep it in sync with any new project types the module
 * starts depending on.
 */

#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>

#ifdef NULL
#undef NULL
#endif
#define NULL (0) /* should be ((void *) 0) */

#ifdef TRUE
#undef TRUE
#endif
#define TRUE (1)

#ifdef FALSE
#undef FALSE
#endif
#define FALSE     (0)

#define M_ERROR   (-1)
#define M_SUCCESS (0)

typedef int8_t   S08; /* !<  8 bits signed integer */
typedef int16_t  S16; /* !< 16 bits signed integer */
typedef int32_t  S32; /* !< 32 bits signed integer */
typedef int64_t  S64; /* !< 64 bits signed integer */

typedef uint8_t  U08; /* !<  8 bits unsigned integer */
typedef uint16_t U16; /* !< 16 bits unsigned integer */
typedef uint32_t U32; /* !< 32 bits unsigned integer */
typedef uint64_t U64; /* !< 64 bits unsigned integer */

typedef uint8_t  BOOL; /* !< boolean */

#endif /* _TYPES_H_ */
