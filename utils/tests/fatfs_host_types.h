/* Match the Dreamcast widths when compiling the pinned FatFs on a 64-bit host.
 * integer.h uses long for DWORD, which is 64-bit on Linux. */
#ifndef _FF_INTEGER
#define _FF_INTEGER
#include <stdint.h>
typedef uint8_t BYTE;
typedef int16_t SHORT;
typedef uint16_t WORD, WCHAR;
typedef int INT;
typedef unsigned int UINT;
typedef int32_t LONG;
typedef uint32_t DWORD;
#endif
