#pragma once

#define USE_BUILTIN 1
#define HAS_BUILTIN(fn) (USE_BUILTIN && __has_builtin(fn))

typedef __uint128_t u128;
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned char u8;
#define INLINE static inline __attribute__((always_inline))

// https://stackoverflow.com/a/32107675/3664464

#define MIN(x, y)                                                                                  \
  ({                                                                                               \
    __auto_type _x = (x);                                                                          \
    __auto_type _y = (y);                                                                          \
    _x < _y ? _x : _y;                                                                             \
  })

#define MAX(x, y)                                                                                  \
  ({                                                                                               \
    __auto_type _x = (x);                                                                          \
    __auto_type _y = (y);                                                                          \
    _x > _y ? _x : _y;                                                                             \
  })

// https://clang.llvm.org/docs/LanguageExtensions.html#:~:text=__builtin_addcll
// https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html#:~:text=__builtin_uaddll_overflow

#if HAS_BUILTIN(__builtin_addcll)
  #define addc64(x, y, carryin, carryout) __builtin_addcll(x, y, carryin, carryout)
#else
  #define addc64(x, y, carryin, carryout)                                                          \
    ({                                                                                             \
      u64 rs;                                                                                      \
      bool overflow1 = __builtin_uaddll_overflow(x, y, &rs);                                       \
      bool overflow2 = __builtin_uaddll_overflow(rs, carryin, &rs);                                \
      *(carryout) = (overflow1 || overflow2) ? 1 : 0;                                              \
      rs;                                                                                          \
    })
#endif

// https://clang.llvm.org/docs/LanguageExtensions.html#:~:text=__builtin_subcll
// https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html#:~:text=__builtin_usubll_overflow

#if HAS_BUILTIN(__builtin_subcll)
  #define subc64(x, y, carryin, carryout) __builtin_subcll(x, y, carryin, carryout)
#else
  #define subc64(x, y, carryin, carryout)                                                          \
    ({                                                                                             \
      u64 rs;                                                                                      \
      bool underflow1 = __builtin_usubll_overflow(x, y, &rs);                                      \
      bool underflow2 = __builtin_usubll_overflow(rs, carryin, &rs);                               \
      *(carryout) = (underflow1 || underflow2) ? 1 : 0;                                            \
      rs;                                                                                          \
    })
#endif

// Other builtins

#if HAS_BUILTIN(__builtin_rotateleft32)
  #define rotl32(x, n) __builtin_rotateleft32(x, n)
#else
  #define rotl32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#endif

#if HAS_BUILTIN(__builtin_bswap32)
  #define swap32(x) __builtin_bswap32(x)
#else
  #define swap32(x) ((x) << 24) | ((x) << 8 & 0x00ff0000) | ((x) >> 8 & 0x0000ff00) | ((x) >> 24)
#endif

#if HAS_BUILTIN(__builtin_bswap64)
  #define swap64(x) __builtin_bswap64(x)
#else
  #define swap64(x)                                                                                \
    ((x) << 56) | ((x) << 40 & 0x00ff000000000000) | ((x) << 24 & 0x0000ff0000000000) |            \
        ((x) << 8 & 0x000000ff00000000) | ((x) >> 8 & 0x00000000ff000000) |                        \
        ((x) >> 24 & 0x0000000000ff0000) | ((x) >> 40 & 0x000000000000ff00) | ((x) >> 56)
#endif
