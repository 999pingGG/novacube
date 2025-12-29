#pragma once
#ifndef _CVKM_H_
#define _CVKM_H_
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef CVKM_ENABLE_FLECS
#include <flecs.h>
#endif

#if !defined(CVKM_LH) && !defined(CVKM_RH)
#define CVKM_RH
#endif

#if !defined(CVKM_ZO) && !defined(CVKM_NO)
#define CVKM_ZO
#endif

#if defined(CVKM_RH)
#if defined(CVKM_ZO)
#define CVKM_RH_ZO
#else
#define CVKM_RH_NO
#endif
#else
#if defined(CVKM_ZO)
#define CVKM_LH_ZO
#else
#define CVKM_LH_NO
#endif
#endif

#define CVKM_E          2.71828182845904523536  // e
#define CVKM_LOG2E      1.44269504088896340736  // log2(e)
#define CVKM_LOG10E     0.434294481903251827651 // log10(e)
#define CVKM_LN2        0.693147180559945309417 // ln(2)
#define CVKM_LN10       2.30258509299404568402  // ln(10)
#define CVKM_PI         3.14159265358979323846  // pi
#define CVKM_PI_2       1.57079632679489661923  // pi/2
#define CVKM_PI_4       0.785398163397448309616 // pi/4
#define CVKM_1_PI       0.318309886183790671538 // 1/pi
#define CVKM_2_PI       0.636619772367581343076 // 2/pi
#define CVKM_2_SQRTPI   1.1283791670955125739   // 2/sqrt(pi)
#define CVKM_SQRT2      1.4142135623730950488   // sqrt(2)
#define CVKM_SQRT1_2    0.707106781186547524401 // 1/sqrt(2)
#define CVKM_DEG2RAD    0.01745329251994329576  // pi/180
#define CVKM_RAD2DEG    57.29577951308232087679 // 180/pi

#define CVKM_E_F          ((float)CVKM_E)
#define CVKM_LOG2E_F      ((float)CVKM_LOG2E)
#define CVKM_LOG10E_F     ((float)CVKM_LOG10E)
#define CVKM_LN2_F        ((float)CVKM_LN2)
#define CVKM_LN10_F       ((float)CVKM_LN10)
#define CVKM_PI_F         ((float)CVKM_PI)
#define CVKM_PI_2_F       ((float)CVKM_PI_2)
#define CVKM_PI_4_F       ((float)CVKM_PI_4)
#define CVKM_1_PI_F       ((float)CVKM_1_PI)
#define CVKM_2_PI_F       ((float)CVKM_2_PI)
#define CVKM_2_SQRTPI_F   ((float)CVKM_2_SQRTPI)
#define CVKM_SQRT2_F      ((float)CVKM_SQRT2)
#define CVKM_SQRT1_2_F    ((float)CVKM_SQRT1_2)
#define CVKM_DEG2RAD_F    ((float)CVKM_DEG2RAD)
#define CVKM_RAD2DEG_F    ((float)CVKM_RAD2DEG)

#ifdef __clang__
// This is left ignored because in some macros using _Generic we list both const and non-const types. Some compilers
// (clang and possibly others...?) throw a warning complaining about unreachable cases because qualifiers (const and
// others) are discarded, so we just tell them to shut up, we use the same function in both variants anyway.
#pragma GCC diagnostic ignored "-Wunreachable-code-generic-assoc"
#endif

#if !defined(_MSC_VER) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define CVKM_DEFINE_VEC2(prefix, type) typedef union vkm_##prefix##vec2 {\
  struct {\
    type x, y;\
  };\
  struct {\
    type r, g;\
  };\
  struct {\
    type s, t;\
  };\
  struct {\
    type u, v;\
  };\
  type raw[2];\
} vkm_##prefix##vec2

#define CVKM_DEFINE_VEC3(prefix, type) typedef union vkm_##prefix##vec3 {\
  struct {\
    type x, y, z;\
  };\
  struct {\
    type r, g, b;\
  };\
  struct {\
    type s, t, p;\
  };\
  type raw[3];\
} vkm_##prefix##vec3

#define CVKM_DEFINE_VEC4(prefix, type) typedef union vkm_##prefix##vec4 {\
  struct {\
    type x, y, z, w;\
  };\
  struct {\
    type r, g, b, a;\
  };\
  struct {\
    type s, t, p, q;\
  };\
  type raw[4];\
} vkm_##prefix##vec4

#define CVKM_DEFINE_RECT(prefix, type) typedef struct vkm_##prefix##rect {\
  type x, y, width, height;\
} vkm_##prefix##rect

CVKM_DEFINE_VEC2(b, int8_t);
CVKM_DEFINE_VEC2(ub, uint8_t);
CVKM_DEFINE_VEC2(s, int16_t);
CVKM_DEFINE_VEC2(us, uint16_t);
CVKM_DEFINE_VEC2(i, int32_t);
CVKM_DEFINE_VEC2(u, uint32_t);
CVKM_DEFINE_VEC2(l, int64_t);
CVKM_DEFINE_VEC2(ul, uint64_t);
CVKM_DEFINE_VEC2(, float);
CVKM_DEFINE_VEC2(d, double);

#define CVKM_VEC2_ZERO_INIT { { 0, 0 } }
#define CVKM_VEC3_ZERO_INIT { { 0, 0, 0 } }
#define CVKM_VEC4_ZERO_INIT { { 0, 0, 0, 0 } }

#define CVKM_VEC2_ONE_INIT { { 1, 1 } }
#define CVKM_VEC3_ONE_INIT { { 1, 1, 1 } }
#define CVKM_VEC4_ONE_INIT { { 1, 1, 1, 1 } }

#define CVKM_BVEC2_ZERO   ((vkm_bvec2)  CVKM_VEC2_ZERO_INIT)
#define CVKM_UBVEC2_ZERO  ((vkm_ubvec2) CVKM_VEC2_ZERO_INIT)
#define CVKM_SVEC2_ZERO   ((vkm_svec2)  CVKM_VEC2_ZERO_INIT)
#define CVKM_USVEC2_ZERO  ((vkm_usvec2) CVKM_VEC2_ZERO_INIT)
#define CVKM_IVEC2_ZERO   ((vkm_ivec2)  CVKM_VEC2_ZERO_INIT)
#define CVKM_UVEC2_ZERO   ((vkm_uvec2)  CVKM_VEC2_ZERO_INIT)
#define CVKM_LVEC2_ZERO   ((vkm_lvec2)  CVKM_VEC2_ZERO_INIT)
#define CVKM_ULVEC2_ZERO  ((vkm_ulvec2) CVKM_VEC2_ZERO_INIT)
#define CVKM_VEC2_ZERO    ((vkm_vec2)   CVKM_VEC2_ZERO_INIT)
#define CVKM_DVEC2_ZERO   ((vkm_dvec2)  CVKM_VEC2_ZERO_INIT)

#define CVKM_BVEC2_ONE    ((vkm_bvec2)  CVKM_VEC2_ONE_INIT)
#define CVKM_UBVEC2_ONE   ((vkm_ubvec2) CVKM_VEC2_ONE_INIT)
#define CVKM_SVEC2_ONE    ((vkm_svec2)  CVKM_VEC2_ONE_INIT)
#define CVKM_USVEC2_ONE   ((vkm_usvec2) CVKM_VEC2_ONE_INIT)
#define CVKM_IVEC2_ONE    ((vkm_ivec2)  CVKM_VEC2_ONE_INIT)
#define CVKM_UVEC2_ONE    ((vkm_uvec2)  CVKM_VEC2_ONE_INIT)
#define CVKM_LVEC2_ONE    ((vkm_lvec2)  CVKM_VEC2_ONE_INIT)
#define CVKM_ULVEC2_ONE   ((vkm_ulvec2) CVKM_VEC2_ONE_INIT)
#define CVKM_VEC2_ONE     ((vkm_vec2)   CVKM_VEC2_ONE_INIT)
#define CVKM_DVEC2_ONE    ((vkm_dvec2)  CVKM_VEC2_ONE_INIT)

#define CVKM_BVEC2_RIGHT  ((vkm_bvec2)  { { 1,    0    } })
#define CVKM_UBVEC2_RIGHT ((vkm_ubvec2) { { 1,    0    } })
#define CVKM_SVEC2_RIGHT  ((vkm_svec2)  { { 1,    0    } })
#define CVKM_USVEC2_RIGHT ((vkm_usvec2) { { 1,    0    } })
#define CVKM_IVEC2_RIGHT  ((vkm_ivec2)  { { 1,    0    } })
#define CVKM_UVEC2_RIGHT  ((vkm_uvec2)  { { 1,    0    } })
#define CVKM_LVEC2_RIGHT  ((vkm_lvec2)  { { 1,    0    } })
#define CVKM_ULVEC2_RIGHT ((vkm_ulvec2) { { 1,    0    } })
#define CVKM_VEC2_RIGHT   ((vkm_vec2)   { { 1.0f, 0.0f } })
#define CVKM_DVEC2_RIGHT  ((vkm_dvec2)  { { 1.0,  0.0f } })

#define CVKM_BVEC2_UP     ((vkm_bvec2)  { { 0,    1    } })
#define CVKM_UBVEC2_UP    ((vkm_ubvec2) { { 0,    1    } })
#define CVKM_SVEC2_UP     ((vkm_svec2)  { { 0,    1    } })
#define CVKM_USVEC2_UP    ((vkm_usvec2) { { 0,    1    } })
#define CVKM_IVEC2_UP     ((vkm_ivec2)  { { 0,    1    } })
#define CVKM_UVEC2_UP     ((vkm_uvec2)  { { 0,    1    } })
#define CVKM_LVEC2_UP     ((vkm_lvec2)  { { 0,    1    } })
#define CVKM_ULVEC2_UP    ((vkm_ulvec2) { { 0,    1    } })
#define CVKM_VEC2_UP      ((vkm_vec2)   { { 0.0f, 1.0f } })
#define CVKM_DVEC2_UP     ((vkm_dvec2)  { { 0.0,  1.0f } })

#define CVKM_BVEC2_LEFT   ((vkm_bvec2)  { { -1,    0    } })
#define CVKM_UBVEC2_LEFT  ((vkm_ubvec2) { { -1,    0    } })
#define CVKM_SVEC2_LEFT   ((vkm_svec2)  { { -1,    0    } })
#define CVKM_USVEC2_LEFT  ((vkm_usvec2) { { -1,    0    } })
#define CVKM_IVEC2_LEFT   ((vkm_ivec2)  { { -1,    0    } })
#define CVKM_UVEC2_LEFT   ((vkm_uvec2)  { { -1,    0    } })
#define CVKM_LVEC2_LEFT   ((vkm_lvec2)  { { -1,    0    } })
#define CVKM_ULVEC2_LEFT  ((vkm_ulvec2) { { -1,    0    } })
#define CVKM_VEC2_LEFT    ((vkm_vec2)   { { -1.0f, 0.0f } })
#define CVKM_DVEC2_LEFT   ((vkm_dvec2)  { { -1.0,  0.0f } })

#define CVKM_BVEC2_DOWN   ((vkm_bvec2)  { { 0,    -1    } })
#define CVKM_UBVEC2_DOWN  ((vkm_ubvec2) { { 0,    -1    } })
#define CVKM_SVEC2_DOWN   ((vkm_svec2)  { { 0,    -1    } })
#define CVKM_USVEC2_DOWN  ((vkm_usvec2) { { 0,    -1    } })
#define CVKM_IVEC2_DOWN   ((vkm_ivec2)  { { 0,    -1    } })
#define CVKM_UVEC2_DOWN   ((vkm_uvec2)  { { 0,    -1    } })
#define CVKM_LVEC2_DOWN   ((vkm_lvec2)  { { 0,    -1    } })
#define CVKM_ULVEC2_DOWN  ((vkm_ulvec2) { { 0,    -1    } })
#define CVKM_VEC2_DOWN    ((vkm_vec2)   { { 0.0f, -1.0f } })
#define CVKM_DVEC2_DOWN   ((vkm_dvec2)  { { 0.0,  -1.0f } })

CVKM_DEFINE_VEC3(b, int8_t);
CVKM_DEFINE_VEC3(ub, uint8_t);
CVKM_DEFINE_VEC3(s, int16_t);
CVKM_DEFINE_VEC3(us, uint16_t);
CVKM_DEFINE_VEC3(i, int32_t);
CVKM_DEFINE_VEC3(u, uint32_t);
CVKM_DEFINE_VEC3(l, int64_t);
CVKM_DEFINE_VEC3(ul, uint64_t);
CVKM_DEFINE_VEC3(, float);
CVKM_DEFINE_VEC3(d, double);

#define CVKM_BVEC3_ZERO   ((vkm_bvec3)  CVKM_VEC3_ZERO_INIT)
#define CVKM_UBVEC3_ZERO  ((vkm_ubvec3) CVKM_VEC3_ZERO_INIT)
#define CVKM_SVEC3_ZERO   ((vkm_svec3)  CVKM_VEC3_ZERO_INIT)
#define CVKM_USVEC3_ZERO  ((vkm_usvec3) CVKM_VEC3_ZERO_INIT)
#define CVKM_IVEC3_ZERO   ((vkm_ivec3)  CVKM_VEC3_ZERO_INIT)
#define CVKM_UVEC3_ZERO   ((vkm_uvec3)  CVKM_VEC3_ZERO_INIT)
#define CVKM_LVEC3_ZERO   ((vkm_lvec3)  CVKM_VEC3_ZERO_INIT)
#define CVKM_ULVEC3_ZERO  ((vkm_ulvec3) CVKM_VEC3_ZERO_INIT)
#define CVKM_VEC3_ZERO    ((vkm_vec3)   CVKM_VEC3_ZERO_INIT)
#define CVKM_DVEC3_ZERO   ((vkm_dvec3)  CVKM_VEC3_ZERO_INIT)

#define CVKM_BVEC3_ONE    ((vkm_bvec3)  CVKM_VEC3_ONE_INIT)
#define CVKM_UBVEC3_ONE   ((vkm_ubvec3) CVKM_VEC3_ONE_INIT)
#define CVKM_SVEC3_ONE    ((vkm_svec3)  CVKM_VEC3_ONE_INIT)
#define CVKM_USVEC3_ONE   ((vkm_usvec3) CVKM_VEC3_ONE_INIT)
#define CVKM_IVEC3_ONE    ((vkm_ivec3)  CVKM_VEC3_ONE_INIT)
#define CVKM_UVEC3_ONE    ((vkm_uvec3)  CVKM_VEC3_ONE_INIT)
#define CVKM_LVEC3_ONE    ((vkm_lvec3)  CVKM_VEC3_ONE_INIT)
#define CVKM_ULVEC3_ONE   ((vkm_ulvec3) CVKM_VEC3_ONE_INIT)
#define CVKM_VEC3_ONE     ((vkm_vec3)   CVKM_VEC3_ONE_INIT)
#define CVKM_DVEC3_ONE    ((vkm_dvec3)  CVKM_VEC3_ONE_INIT)

#define CVKM_BVEC3_RIGHT  ((vkm_bvec3)  { { 1,    0,    0    } })
#define CVKM_UBVEC3_RIGHT ((vkm_ubvec3) { { 1,    0,    0    } })
#define CVKM_SVEC3_RIGHT  ((vkm_svec3)  { { 1,    0,    0    } })
#define CVKM_USVEC3_RIGHT ((vkm_usvec3) { { 1,    0,    0    } })
#define CVKM_IVEC3_RIGHT  ((vkm_ivec3)  { { 1,    0,    0    } })
#define CVKM_UVEC3_RIGHT  ((vkm_uvec3)  { { 1,    0,    0    } })
#define CVKM_LVEC3_RIGHT  ((vkm_lvec3)  { { 1,    0,    0    } })
#define CVKM_ULVEC3_RIGHT ((vkm_ulvec3) { { 1,    0,    0    } })
#define CVKM_VEC3_RIGHT   ((vkm_vec3)   { { 1.0f, 0.0f, 0.0f } })
#define CVKM_DVEC3_RIGHT  ((vkm_dvec3)  { { 1.0,  0.0,  0.0  } })

#define CVKM_BVEC3_UP     ((vkm_bvec3)  { { 0,    1,    0    } })
#define CVKM_UBVEC3_UP    ((vkm_ubvec3) { { 0,    1,    0    } })
#define CVKM_SVEC3_UP     ((vkm_svec3)  { { 0,    1,    0    } })
#define CVKM_USVEC3_UP    ((vkm_usvec3) { { 0,    1,    0    } })
#define CVKM_IVEC3_UP     ((vkm_ivec3)  { { 0,    1,    0    } })
#define CVKM_UVEC3_UP     ((vkm_uvec3)  { { 0,    1,    0    } })
#define CVKM_LVEC3_UP     ((vkm_lvec3)  { { 0,    1,    0    } })
#define CVKM_ULVEC3_UP    ((vkm_ulvec3) { { 0,    1,    0    } })
#define CVKM_VEC3_UP      ((vkm_vec3)   { { 0.0f, 1.0f, 0.0f } })
#define CVKM_DVEC3_UP     ((vkm_dvec3)  { { 0.0,  1.0,  0.0  } })

#ifdef CVKM_LH
#define CVKM_BVEC3_FRONT  ((vkm_bvec3)  { { 0,    0,    1    } })
#define CVKM_UBVEC3_FRONT ((vkm_ubvec3) { { 0,    0,    1    } })
#define CVKM_SVEC3_FRONT  ((vkm_svec3)  { { 0,    0,    1    } })
#define CVKM_USVEC3_FRONT ((vkm_usvec3) { { 0,    0,    1    } })
#define CVKM_IVEC3_FRONT  ((vkm_ivec3)  { { 0,    0,    1    } })
#define CVKM_UVEC3_FRONT  ((vkm_uvec3)  { { 0,    0,    1    } })
#define CVKM_LVEC3_FRONT  ((vkm_lvec3)  { { 0,    0,    1    } })
#define CVKM_ULVEC3_FRONT ((vkm_ulvec3) { { 0,    0,    1    } })
#define CVKM_VEC3_FRONT   ((vkm_vec3)   { { 0.0f, 0.0f, 1.0f } })
#define CVKM_DVEC3_FRONT  ((vkm_dvec3)  { { 0.0,  0.0,  1.0  } })
#else
#define CVKM_BVEC3_FRONT  ((vkm_bvec3)  { { 0,    0,    -1    } })
#define CVKM_UBVEC3_FRONT ((vkm_ubvec3) { { 0,    0,    -1    } })
#define CVKM_SVEC3_FRONT  ((vkm_svec3)  { { 0,    0,    -1    } })
#define CVKM_USVEC3_FRONT ((vkm_usvec3) { { 0,    0,    -1    } })
#define CVKM_IVEC3_FRONT  ((vkm_ivec3)  { { 0,    0,    -1    } })
#define CVKM_UVEC3_FRONT  ((vkm_uvec3)  { { 0,    0,    -1    } })
#define CVKM_LVEC3_FRONT  ((vkm_lvec3)  { { 0,    0,    -1    } })
#define CVKM_ULVEC3_FRONT ((vkm_ulvec3) { { 0,    0,    -1    } })
#define CVKM_VEC3_FRONT   ((vkm_vec3)   { { 0.0f, 0.0f, -1.0f } })
#define CVKM_DVEC3_FRONT  ((vkm_dvec3)  { { 0.0,  0.0,  -1.0  } })
#endif

#define CVKM_BVEC3_LEFT   ((vkm_bvec3)  { { -1,    0,    0    } })
#define CVKM_UBVEC3_LEFT  ((vkm_ubvec3) { { -1,    0,    0    } })
#define CVKM_SVEC3_LEFT   ((vkm_svec3)  { { -1,    0,    0    } })
#define CVKM_USVEC3_LEFT  ((vkm_usvec3) { { -1,    0,    0    } })
#define CVKM_IVEC3_LEFT   ((vkm_ivec3)  { { -1,    0,    0    } })
#define CVKM_UVEC3_LEFT   ((vkm_uvec3)  { { -1,    0,    0    } })
#define CVKM_LVEC3_LEFT   ((vkm_lvec3)  { { -1,    0,    0    } })
#define CVKM_ULVEC3_LEFT  ((vkm_ulvec3) { { -1,    0,    0    } })
#define CVKM_VEC3_LEFT    ((vkm_vec3)   { { -1.0f, 0.0f, 0.0f } })
#define CVKM_DVEC3_LEFT   ((vkm_dvec3)  { { -1.0,  0.0,  0.0  } })

#define CVKM_BVEC3_DOWN   ((vkm_bvec3)  { { 0,    -1,    0    } })
#define CVKM_UBVEC3_DOWN  ((vkm_ubvec3) { { 0,    -1,    0    } })
#define CVKM_SVEC3_DOWN   ((vkm_svec3)  { { 0,    -1,    0    } })
#define CVKM_USVEC3_DOWN  ((vkm_usvec3) { { 0,    -1,    0    } })
#define CVKM_IVEC3_DOWN   ((vkm_ivec3)  { { 0,    -1,    0    } })
#define CVKM_UVEC3_DOWN   ((vkm_uvec3)  { { 0,    -1,    0    } })
#define CVKM_LVEC3_DOWN   ((vkm_lvec3)  { { 0,    -1,    0    } })
#define CVKM_ULVEC3_DOWN  ((vkm_ulvec3) { { 0,    -1,    0    } })
#define CVKM_VEC3_DOWN    ((vkm_vec3)   { { 0.0f, -1.0f, 0.0f } })
#define CVKM_DVEC3_DOWN   ((vkm_dvec3)  { { 0.0,  -1.0f, 0.0  } })

#ifdef CVKM_LH
#define CVKM_BVEC3_BACK   ((vkm_bvec3)  { { 0,    0,    1    } })
#define CVKM_UBVEC3_BACK  ((vkm_ubvec3) { { 0,    0,    1    } })
#define CVKM_SVEC3_BACK   ((vkm_svec3)  { { 0,    0,    1    } })
#define CVKM_USVEC3_BACK  ((vkm_usvec3) { { 0,    0,    1    } })
#define CVKM_IVEC3_BACK   ((vkm_ivec3)  { { 0,    0,    1    } })
#define CVKM_UVEC3_BACK   ((vkm_uvec3)  { { 0,    0,    1    } })
#define CVKM_LVEC3_BACK   ((vkm_lvec3)  { { 0,    0,    1    } })
#define CVKM_ULVEC3_BACK  ((vkm_ulvec3) { { 0,    0,    1    } })
#define CVKM_VEC3_BACK    ((vkm_vec3)   { { 0.0f, 0.0f, 1.0f } })
#define CVKM_DVEC3_BACK   ((vkm_dvec3)  { { 0.0,  0.0,  1.0  } })
#else
#define CVKM_BVEC3_BACK   ((vkm_bvec3)  { { 0,    0,    -1    } })
#define CVKM_UBVEC3_BACK  ((vkm_ubvec3) { { 0,    0,    -1    } })
#define CVKM_SVEC3_BACK   ((vkm_svec3)  { { 0,    0,    -1    } })
#define CVKM_USVEC3_BACK  ((vkm_usvec3) { { 0,    0,    -1    } })
#define CVKM_IVEC3_BACK   ((vkm_ivec3)  { { 0,    0,    -1    } })
#define CVKM_UVEC3_BACK   ((vkm_uvec3)  { { 0,    0,    -1    } })
#define CVKM_LVEC3_BACK   ((vkm_lvec3)  { { 0,    0,    -1    } })
#define CVKM_ULVEC3_BACK  ((vkm_ulvec3) { { 0,    0,    -1    } })
#define CVKM_VEC3_BACK    ((vkm_vec3)   { { 0.0f, 0.0f, -1.0f } })
#define CVKM_DVEC3_BACK   ((vkm_dvec3)  { { 0.0,  0.0,  -1.0  } })
#endif

CVKM_DEFINE_VEC4(b, int8_t);
CVKM_DEFINE_VEC4(ub, uint8_t);
CVKM_DEFINE_VEC4(s, int16_t);
CVKM_DEFINE_VEC4(us, uint16_t);
CVKM_DEFINE_VEC4(i, int32_t);
CVKM_DEFINE_VEC4(u, uint32_t);
CVKM_DEFINE_VEC4(l, int64_t);
CVKM_DEFINE_VEC4(ul, uint64_t);
CVKM_DEFINE_VEC4(, float);
CVKM_DEFINE_VEC4(d, double);

#define CVKM_BVEC4_ZERO   ((vkm_bvec4)  CVKM_VEC4_ZERO_INIT)
#define CVKM_UBVEC4_ZERO  ((vkm_ubvec4) CVKM_VEC4_ZERO_INIT)
#define CVKM_SVEC4_ZERO   ((vkm_svec4)  CVKM_VEC4_ZERO_INIT)
#define CVKM_USVEC4_ZERO  ((vkm_usvec4) CVKM_VEC4_ZERO_INIT)
#define CVKM_IVEC4_ZERO   ((vkm_ivec4)  CVKM_VEC4_ZERO_INIT)
#define CVKM_UVEC4_ZERO   ((vkm_uvec4)  CVKM_VEC4_ZERO_INIT)
#define CVKM_LVEC4_ZERO   ((vkm_lvec4)  CVKM_VEC4_ZERO_INIT)
#define CVKM_ULVEC4_ZERO  ((vkm_ulvec4) CVKM_VEC4_ZERO_INIT)
#define CVKM_VEC4_ZERO    ((vkm_vec4)   CVKM_VEC4_ZERO_INIT)
#define CVKM_DVEC4_ZERO   ((vkm_dvec4)  CVKM_VEC4_ZERO_INIT)

#define CVKM_BVEC4_ONE    ((vkm_bvec4)  CVKM_VEC4_ONE_INIT)
#define CVKM_UBVEC4_ONE   ((vkm_ubvec4) CVKM_VEC4_ONE_INIT)
#define CVKM_SVEC4_ONE    ((vkm_svec4)  CVKM_VEC4_ONE_INIT)
#define CVKM_USVEC4_ONE   ((vkm_usvec4) CVKM_VEC4_ONE_INIT)
#define CVKM_IVEC4_ONE    ((vkm_ivec4)  CVKM_VEC4_ONE_INIT)
#define CVKM_UVEC4_ONE    ((vkm_uvec4)  CVKM_VEC4_ONE_INIT)
#define CVKM_LVEC4_ONE    ((vkm_lvec4)  CVKM_VEC4_ONE_INIT)
#define CVKM_ULVEC4_ONE   ((vkm_ulvec4) CVKM_VEC4_ONE_INIT)
#define CVKM_VEC4_ONE     ((vkm_vec4)   CVKM_VEC4_ONE_INIT)
#define CVKM_DVEC4_ONE    ((vkm_dvec4)  CVKM_VEC4_ONE_INIT)

CVKM_DEFINE_RECT(b, int8_t);
CVKM_DEFINE_RECT(ub, uint8_t);
CVKM_DEFINE_RECT(s, int16_t);
CVKM_DEFINE_RECT(us, uint16_t);
CVKM_DEFINE_RECT(i, int32_t);
CVKM_DEFINE_RECT(u, uint32_t);
CVKM_DEFINE_RECT(l, int64_t);
CVKM_DEFINE_RECT(ul, uint64_t);
CVKM_DEFINE_RECT(, float);
CVKM_DEFINE_RECT(d, double);

typedef union vkm_quat {
  struct {
    float x, y, z, w;
  };
  float raw[4];
} vkm_quat;

typedef vkm_quat vkm_versor;

#define CVKM_QUAT_IDENTITY ((vkm_quat){ { 0.0f, 0.0f, 0.0f, 1.0f } })

typedef union vkm_mat3 {
  vkm_vec3 columns[3];
  struct {
    float m00, m01, m02;
    float m10, m11, m12;
    float m20, m21, m22;
  };
  float raw[9];
} vkm_mat3;

typedef union vkm_mat4 {
  vkm_vec4 columns[4];
  struct {
    float m00, m01, m02, m03;
    float m10, m11, m12, m13;
    float m20, m21, m22, m23;
    float m30, m31, m32, m33;
  };
  float raw[16];
} vkm_mat4;

#define CVKM_MAT3_IDENTITY_INIT { {\
  { { 1.0f, 0.0f, 0.0f } },\
  { { 0.0f, 1.0f, 0.0f } },\
  { { 0.0f, 0.0f, 1.0f } },\
} }

#define CVKM_MAT3_IDENTITY (vkm_mat3)CVKM_MAT3_IDENTITY_INIT

#define CVKM_MAT4_IDENTITY_INIT { {\
  { { 1.0f, 0.0f, 0.0f, 0.0f } },\
  { { 0.0f, 1.0f, 0.0f, 0.0f } },\
  { { 0.0f, 0.0f, 1.0f, 0.0f } },\
  { { 0.0f, 0.0f, 0.0f, 1.0f } },\
} }

#define CVKM_MAT4_IDENTITY (vkm_mat4)CVKM_MAT4_IDENTITY_INIT

#define CVKM_VEC2_OPERATION(type, operation, operator) static void vkm_##type##_##operation(\
  const vkm_##type* a,\
  const vkm_##type* b,\
  vkm_##type* result\
) {\
  *result = (vkm_##type){ { a->x operator b->x, a->y operator b->y } };\
}

#define CVKM_VEC2_MULADD_OPERATIONS(type, scalar_type) static void vkm_##type##_muladd(\
  const vkm_##type* a,\
  const vkm_##type* b,\
  vkm_##type* result\
) {\
  result->x += a->x * b->x;\
  result->y += a->y * b->y;\
}\
\
static void vkm_##type##_muladd_scalar(\
  const vkm_##type* vector,\
  const scalar_type scalar,\
  vkm_##type* result\
) {\
  result->x += vector->x * scalar;\
  result->y += vector->y * scalar;\
}

#define CVKM_VEC2_SCALAR_OPERATION(vec_type, scalar_type, operation, operator)\
static void vkm_##vec_type##_##operation##_scalar(\
  const vkm_##vec_type* vec,\
  const scalar_type scalar,\
  vkm_##vec_type* result\
) {\
  *result = (vkm_##vec_type){ { vec->x operator scalar, vec->y operator scalar } };\
}

#define CVKM_VEC2_ALL_OPERATIONS(vec_type, scalar_type) CVKM_VEC2_OPERATION(vec_type, add, +)\
  CVKM_VEC2_OPERATION(vec_type, sub, -)\
  CVKM_VEC2_OPERATION(vec_type, mul, *)\
  CVKM_VEC2_OPERATION(vec_type, div, /)\
  CVKM_VEC2_MULADD_OPERATIONS(vec_type, scalar_type)\
  CVKM_VEC2_SCALAR_OPERATION(vec_type, scalar_type, add, +)\
  CVKM_VEC2_SCALAR_OPERATION(vec_type, scalar_type, sub, -)\
  CVKM_VEC2_SCALAR_OPERATION(vec_type, scalar_type, mul, *)\
  CVKM_VEC2_SCALAR_OPERATION(vec_type, scalar_type, div, /)

CVKM_VEC2_ALL_OPERATIONS(bvec2, int8_t)
CVKM_VEC2_ALL_OPERATIONS(ubvec2, uint8_t)
CVKM_VEC2_ALL_OPERATIONS(svec2, int16_t)
CVKM_VEC2_ALL_OPERATIONS(usvec2, uint16_t)
CVKM_VEC2_ALL_OPERATIONS(ivec2, int32_t)
CVKM_VEC2_ALL_OPERATIONS(uvec2, uint32_t)
CVKM_VEC2_ALL_OPERATIONS(lvec2, int64_t)
CVKM_VEC2_ALL_OPERATIONS(ulvec2, uint64_t)
CVKM_VEC2_ALL_OPERATIONS(vec2, float)
CVKM_VEC2_ALL_OPERATIONS(dvec2, double)

#define CVKM_VEC3_OPERATION(type, operation, operator) static void vkm_##type##_##operation(\
  const vkm_##type* a,\
  const vkm_##type* b,\
  vkm_##type* result\
) {\
  *result = (vkm_##type){ { a->x operator b->x, a->y operator b->y, a->z operator b->z } };\
}

#define CVKM_VEC3_MULADD_OPERATIONS(type, scalar_type) static void vkm_##type##_muladd(\
  const vkm_##type* a,\
  const vkm_##type* b,\
  vkm_##type* result\
) {\
  result->x += a->x * b->x;\
  result->y += a->y * b->y;\
  result->z += a->z * b->z;\
}\
\
static void vkm_##type##_muladd_scalar(\
  const vkm_##type* vector,\
  const scalar_type scalar,\
  vkm_##type* result\
) {\
  result->x += vector->x * scalar;\
  result->y += vector->y * scalar;\
  result->z += vector->z * scalar;\
}

#define CVKM_VEC3_SCALAR_OPERATION(vec_type, scalar_type, operation, operator)\
static void vkm_##vec_type##_##operation##_scalar(\
  const vkm_##vec_type* vec,\
  const scalar_type scalar,\
  vkm_##vec_type* result\
) {\
  *result = (vkm_##vec_type){ { vec->x operator scalar, vec->y operator scalar, vec->z operator scalar } };\
}

#define CVKM_VEC3_ALL_OPERATIONS(vec_type, scalar_type) CVKM_VEC3_OPERATION(vec_type, add, +)\
  CVKM_VEC3_OPERATION(vec_type, sub, -)\
  CVKM_VEC3_OPERATION(vec_type, mul, *)\
  CVKM_VEC3_OPERATION(vec_type, div, /)\
  CVKM_VEC3_MULADD_OPERATIONS(vec_type, scalar_type)\
  CVKM_VEC3_SCALAR_OPERATION(vec_type, scalar_type, add, +)\
  CVKM_VEC3_SCALAR_OPERATION(vec_type, scalar_type, sub, -)\
  CVKM_VEC3_SCALAR_OPERATION(vec_type, scalar_type, mul, *)\
  CVKM_VEC3_SCALAR_OPERATION(vec_type, scalar_type, div, /)

CVKM_VEC3_ALL_OPERATIONS(bvec3, int8_t)
CVKM_VEC3_ALL_OPERATIONS(ubvec3, uint8_t)
CVKM_VEC3_ALL_OPERATIONS(svec3, int16_t)
CVKM_VEC3_ALL_OPERATIONS(usvec3, uint16_t)
CVKM_VEC3_ALL_OPERATIONS(ivec3, int32_t)
CVKM_VEC3_ALL_OPERATIONS(uvec3, uint32_t)
CVKM_VEC3_ALL_OPERATIONS(lvec3, int64_t)
CVKM_VEC3_ALL_OPERATIONS(ulvec3, uint64_t)
CVKM_VEC3_ALL_OPERATIONS(vec3, float)
CVKM_VEC3_ALL_OPERATIONS(dvec3, double)

#define CVKM_VEC4_OPERATION(type, operation, operator) static void vkm_##type##_##operation(\
  const vkm_##type* a,\
  const vkm_##type* b,\
  vkm_##type* result\
) {\
  *result = (vkm_##type){ { a->x operator b->x, a->y operator b->y, a->z operator b->z, a->w operator b->w } };\
}

#define CVKM_VEC4_MULADD_OPERATIONS(type, scalar_type) static void vkm_##type##_muladd(\
  const vkm_##type* a,\
  const vkm_##type* b,\
  vkm_##type* result\
) {\
  result->x += a->x * b->x;\
  result->y += a->y * b->y;\
  result->z += a->z * b->z;\
  result->w += a->w * b->w;\
}\
\
static void vkm_##type##_muladd_scalar(\
  const vkm_##type* vector,\
  const scalar_type scalar,\
  vkm_##type* result\
) {\
  result->x += vector->x * scalar;\
  result->y += vector->y * scalar;\
  result->z += vector->z * scalar;\
  result->w += vector->w * scalar;\
}

#define CVKM_VEC4_SCALAR_OPERATION(vec_type, scalar_type, operation, operator)\
static void vkm_##vec_type##_##operation##_scalar(\
  const vkm_##vec_type* vec,\
  const scalar_type scalar,\
  vkm_##vec_type* result\
) {\
  *result = (vkm_##vec_type){ {\
    vec->x operator scalar,\
    vec->y operator scalar,\
    vec->z operator scalar,\
    vec->w operator scalar,\
  } };\
}

#define CVKM_VEC4_ALL_OPERATIONS(vec_type, scalar_type) CVKM_VEC4_OPERATION(vec_type, add, +)\
  CVKM_VEC4_OPERATION(vec_type, sub, -)\
  CVKM_VEC4_OPERATION(vec_type, mul, *)\
  CVKM_VEC4_OPERATION(vec_type, div, /)\
  CVKM_VEC4_MULADD_OPERATIONS(vec_type, scalar_type)\
  CVKM_VEC4_SCALAR_OPERATION(vec_type, scalar_type, add, +)\
  CVKM_VEC4_SCALAR_OPERATION(vec_type, scalar_type, sub, -)\
  CVKM_VEC4_SCALAR_OPERATION(vec_type, scalar_type, mul, *)\
  CVKM_VEC4_SCALAR_OPERATION(vec_type, scalar_type, div, /)

CVKM_VEC4_ALL_OPERATIONS(bvec4, int8_t)
CVKM_VEC4_ALL_OPERATIONS(ubvec4, uint8_t)
CVKM_VEC4_ALL_OPERATIONS(svec4, int16_t)
CVKM_VEC4_ALL_OPERATIONS(usvec4, uint16_t)
CVKM_VEC4_ALL_OPERATIONS(ivec4, int32_t)
CVKM_VEC4_ALL_OPERATIONS(uvec4, uint32_t)
CVKM_VEC4_ALL_OPERATIONS(lvec4, int64_t)
CVKM_VEC4_ALL_OPERATIONS(ulvec4, uint64_t)
CVKM_VEC4_ALL_OPERATIONS(vec4, float)
CVKM_VEC4_ALL_OPERATIONS(dvec4, double)

static void vkm_quat_mul(const vkm_quat* p, const vkm_quat* q, vkm_quat* result) {
  const vkm_quat p_copy = *p;
  const vkm_quat q_copy = *q;

  result->x = p_copy.w * q_copy.x + p_copy.x * q_copy.w + p_copy.y * q_copy.z - p_copy.z * q_copy.y;
  result->y = p_copy.w * q_copy.y - p_copy.x * q_copy.z + p_copy.y * q_copy.w + p_copy.z * q_copy.x;
  result->z = p_copy.w * q_copy.z + p_copy.x * q_copy.y - p_copy.y * q_copy.x + p_copy.z * q_copy.w;
  result->w = p_copy.w * q_copy.w - p_copy.x * q_copy.x - p_copy.y * q_copy.y - p_copy.z * q_copy.z;
}

#define CVKM_BASIC_OPERATIONS(vec_type, scalar_type, operation, b) vkm_##vec_type*: _Generic(b,\
  vkm_##vec_type*: vkm_##vec_type##_##operation,\
  const vkm_##vec_type*: vkm_##vec_type##_##operation,\
  scalar_type: vkm_##vec_type##_##operation##_scalar,\
  const scalar_type: vkm_##vec_type##_##operation##_scalar,\
  default: ((void)0)\
)

#define vkm_add(a, b, result) _Generic(result,\
  CVKM_BASIC_OPERATIONS(bvec2, int8_t, add, b),\
  CVKM_BASIC_OPERATIONS(ubvec2, uint8_t, add, b),\
  CVKM_BASIC_OPERATIONS(svec2, int16_t, add, b),\
  CVKM_BASIC_OPERATIONS(usvec2, uint16_t, add, b),\
  CVKM_BASIC_OPERATIONS(ivec2, int32_t, add, b),\
  CVKM_BASIC_OPERATIONS(uvec2, uint32_t, add, b),\
  CVKM_BASIC_OPERATIONS(lvec2, int64_t, add, b),\
  CVKM_BASIC_OPERATIONS(ulvec2, uint64_t, add, b),\
  CVKM_BASIC_OPERATIONS(vec2, float, add, b),\
  CVKM_BASIC_OPERATIONS(dvec2, double, add, b),\
  CVKM_BASIC_OPERATIONS(bvec3, int8_t, add, b),\
  CVKM_BASIC_OPERATIONS(ubvec3, uint8_t, add, b),\
  CVKM_BASIC_OPERATIONS(svec3, int16_t, add, b),\
  CVKM_BASIC_OPERATIONS(usvec3, uint16_t, add, b),\
  CVKM_BASIC_OPERATIONS(ivec3, int32_t, add, b),\
  CVKM_BASIC_OPERATIONS(uvec3, uint32_t, add, b),\
  CVKM_BASIC_OPERATIONS(lvec3, int64_t, add, b),\
  CVKM_BASIC_OPERATIONS(ulvec3, uint64_t, add, b),\
  CVKM_BASIC_OPERATIONS(vec3, float, add, b),\
  CVKM_BASIC_OPERATIONS(dvec3, double, add, b),\
  CVKM_BASIC_OPERATIONS(bvec4, int8_t, add, b),\
  CVKM_BASIC_OPERATIONS(ubvec4, uint8_t, add, b),\
  CVKM_BASIC_OPERATIONS(svec4, int16_t, add, b),\
  CVKM_BASIC_OPERATIONS(usvec4, uint16_t, add, b),\
  CVKM_BASIC_OPERATIONS(ivec4, int32_t, add, b),\
  CVKM_BASIC_OPERATIONS(uvec4, uint32_t, add, b),\
  CVKM_BASIC_OPERATIONS(lvec4, int64_t, add, b),\
  CVKM_BASIC_OPERATIONS(ulvec4, uint64_t, add, b),\
  CVKM_BASIC_OPERATIONS(vec4, float, add, b),\
  CVKM_BASIC_OPERATIONS(dvec4, double, add, b)\
)(a, b, result)

#define vkm_sub(a, b, result) _Generic(result,\
  CVKM_BASIC_OPERATIONS(bvec2, int8_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ubvec2, uint8_t, sub, b),\
  CVKM_BASIC_OPERATIONS(svec2, int16_t, sub, b),\
  CVKM_BASIC_OPERATIONS(usvec2, uint16_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ivec2, int32_t, sub, b),\
  CVKM_BASIC_OPERATIONS(uvec2, uint32_t, sub, b),\
  CVKM_BASIC_OPERATIONS(lvec2, int64_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ulvec2, uint64_t, sub, b),\
  CVKM_BASIC_OPERATIONS(vec2, float, sub, b),\
  CVKM_BASIC_OPERATIONS(dvec2, double, sub, b),\
  CVKM_BASIC_OPERATIONS(bvec3, int8_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ubvec3, uint8_t, sub, b),\
  CVKM_BASIC_OPERATIONS(svec3, int16_t, sub, b),\
  CVKM_BASIC_OPERATIONS(usvec3, uint16_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ivec3, int32_t, sub, b),\
  CVKM_BASIC_OPERATIONS(uvec3, uint32_t, sub, b),\
  CVKM_BASIC_OPERATIONS(lvec3, int64_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ulvec3, uint64_t, sub, b),\
  CVKM_BASIC_OPERATIONS(vec3, float, sub, b),\
  CVKM_BASIC_OPERATIONS(dvec3, double, sub, b),\
  CVKM_BASIC_OPERATIONS(bvec4, int8_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ubvec4, uint8_t, sub, b),\
  CVKM_BASIC_OPERATIONS(svec4, int16_t, sub, b),\
  CVKM_BASIC_OPERATIONS(usvec4, uint16_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ivec4, int32_t, sub, b),\
  CVKM_BASIC_OPERATIONS(uvec4, uint32_t, sub, b),\
  CVKM_BASIC_OPERATIONS(lvec4, int64_t, sub, b),\
  CVKM_BASIC_OPERATIONS(ulvec4, uint64_t, sub, b),\
  CVKM_BASIC_OPERATIONS(vec4, float, sub, b),\
  CVKM_BASIC_OPERATIONS(dvec4, double, sub, b)\
)(a, b, result)

#define vkm_mul(a, b, result) _Generic(result,\
  CVKM_BASIC_OPERATIONS(bvec2, int8_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ubvec2, uint8_t, mul, b),\
  CVKM_BASIC_OPERATIONS(svec2, int16_t, mul, b),\
  CVKM_BASIC_OPERATIONS(usvec2, uint16_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ivec2, int32_t, mul, b),\
  CVKM_BASIC_OPERATIONS(uvec2, uint32_t, mul, b),\
  CVKM_BASIC_OPERATIONS(lvec2, int64_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ulvec2, uint64_t, mul, b),\
  CVKM_BASIC_OPERATIONS(vec2, float, mul, b),\
  CVKM_BASIC_OPERATIONS(dvec2, double, mul, b),\
  CVKM_BASIC_OPERATIONS(bvec3, int8_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ubvec3, uint8_t, mul, b),\
  CVKM_BASIC_OPERATIONS(svec3, int16_t, mul, b),\
  CVKM_BASIC_OPERATIONS(usvec3, uint16_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ivec3, int32_t, mul, b),\
  CVKM_BASIC_OPERATIONS(uvec3, uint32_t, mul, b),\
  CVKM_BASIC_OPERATIONS(lvec3, int64_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ulvec3, uint64_t, mul, b),\
  CVKM_BASIC_OPERATIONS(vec3, float, mul, b),\
  CVKM_BASIC_OPERATIONS(dvec3, double, mul, b),\
  CVKM_BASIC_OPERATIONS(bvec4, int8_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ubvec4, uint8_t, mul, b),\
  CVKM_BASIC_OPERATIONS(svec4, int16_t, mul, b),\
  CVKM_BASIC_OPERATIONS(usvec4, uint16_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ivec4, int32_t, mul, b),\
  CVKM_BASIC_OPERATIONS(uvec4, uint32_t, mul, b),\
  CVKM_BASIC_OPERATIONS(lvec4, int64_t, mul, b),\
  CVKM_BASIC_OPERATIONS(ulvec4, uint64_t, mul, b),\
  CVKM_BASIC_OPERATIONS(vec4, float, mul, b),\
  CVKM_BASIC_OPERATIONS(dvec4, double, mul, b),\
  vkm_mat4*: vkm_mat4_mul,\
  vkm_quat*: vkm_quat_mul\
)(a, b, result)

#define vkm_div(a, b, result) _Generic(result,\
  CVKM_BASIC_OPERATIONS(bvec2, int8_t, div, b),\
  CVKM_BASIC_OPERATIONS(ubvec2, uint8_t, div, b),\
  CVKM_BASIC_OPERATIONS(svec2, int16_t, div, b),\
  CVKM_BASIC_OPERATIONS(usvec2, uint16_t, div, b),\
  CVKM_BASIC_OPERATIONS(ivec2, int32_t, div, b),\
  CVKM_BASIC_OPERATIONS(uvec2, uint32_t, div, b),\
  CVKM_BASIC_OPERATIONS(lvec2, int64_t, div, b),\
  CVKM_BASIC_OPERATIONS(ulvec2, uint64_t, div, b),\
  CVKM_BASIC_OPERATIONS(vec2, float, div, b),\
  CVKM_BASIC_OPERATIONS(dvec2, double, div, b),\
  CVKM_BASIC_OPERATIONS(bvec3, int8_t, div, b),\
  CVKM_BASIC_OPERATIONS(ubvec3, uint8_t, div, b),\
  CVKM_BASIC_OPERATIONS(svec3, int16_t, div, b),\
  CVKM_BASIC_OPERATIONS(usvec3, uint16_t, div, b),\
  CVKM_BASIC_OPERATIONS(ivec3, int32_t, div, b),\
  CVKM_BASIC_OPERATIONS(uvec3, uint32_t, div, b),\
  CVKM_BASIC_OPERATIONS(lvec3, int64_t, div, b),\
  CVKM_BASIC_OPERATIONS(ulvec3, uint64_t, div, b),\
  CVKM_BASIC_OPERATIONS(vec3, float, div, b),\
  CVKM_BASIC_OPERATIONS(dvec3, double, div, b),\
  CVKM_BASIC_OPERATIONS(bvec4, int8_t, div, b),\
  CVKM_BASIC_OPERATIONS(ubvec4, uint8_t, div, b),\
  CVKM_BASIC_OPERATIONS(svec4, int16_t, div, b),\
  CVKM_BASIC_OPERATIONS(usvec4, uint16_t, div, b),\
  CVKM_BASIC_OPERATIONS(ivec4, int32_t, div, b),\
  CVKM_BASIC_OPERATIONS(uvec4, uint32_t, div, b),\
  CVKM_BASIC_OPERATIONS(lvec4, int64_t, div, b),\
  CVKM_BASIC_OPERATIONS(ulvec4, uint64_t, div, b),\
  CVKM_BASIC_OPERATIONS(vec4, float, div, b),\
  CVKM_BASIC_OPERATIONS(dvec4, double, div, b)\
)(a, b, result)

#define CVKM_MULADD_OPERATIONS(vector_type, scalar_type, b) vkm_##vector_type*: _Generic(b,\
  vkm_##vector_type*: vkm_##vector_type##_muladd,\
  const vkm_##vector_type*: vkm_##vector_type##_muladd,\
  scalar_type: vkm_##vector_type##_muladd_scalar,\
  const scalar_type: vkm_##vector_type##_muladd_scalar,\
  default: ((void)0)\
)

#define vkm_muladd(a, b, result) _Generic(result,\
  CVKM_MULADD_OPERATIONS(bvec2, int8_t, b),\
  CVKM_MULADD_OPERATIONS(ubvec2, uint8_t, b),\
  CVKM_MULADD_OPERATIONS(svec2, int16_t, b),\
  CVKM_MULADD_OPERATIONS(usvec2, uint16_t, b),\
  CVKM_MULADD_OPERATIONS(ivec2, int32_t, b),\
  CVKM_MULADD_OPERATIONS(uvec2, uint32_t, b),\
  CVKM_MULADD_OPERATIONS(lvec2, int64_t, b),\
  CVKM_MULADD_OPERATIONS(ulvec2, uint64_t, b),\
  CVKM_MULADD_OPERATIONS(vec2, float, b),\
  CVKM_MULADD_OPERATIONS(dvec2, double, b),\
  CVKM_MULADD_OPERATIONS(bvec3, int8_t, b),\
  CVKM_MULADD_OPERATIONS(ubvec3, uint8_t, b),\
  CVKM_MULADD_OPERATIONS(svec3, int16_t, b),\
  CVKM_MULADD_OPERATIONS(usvec3, uint16_t, b),\
  CVKM_MULADD_OPERATIONS(ivec3, int32_t, b),\
  CVKM_MULADD_OPERATIONS(uvec3, uint32_t, b),\
  CVKM_MULADD_OPERATIONS(lvec3, int64_t, b),\
  CVKM_MULADD_OPERATIONS(ulvec3, uint64_t, b),\
  CVKM_MULADD_OPERATIONS(vec3, float, b),\
  CVKM_MULADD_OPERATIONS(dvec3, double, b),\
  CVKM_MULADD_OPERATIONS(bvec4, int8_t, b),\
  CVKM_MULADD_OPERATIONS(ubvec4, uint8_t, b),\
  CVKM_MULADD_OPERATIONS(svec4, int16_t, b),\
  CVKM_MULADD_OPERATIONS(usvec4, uint16_t, b),\
  CVKM_MULADD_OPERATIONS(ivec4, int32_t, b),\
  CVKM_MULADD_OPERATIONS(uvec4, uint32_t, b),\
  CVKM_MULADD_OPERATIONS(lvec4, int64_t, b),\
  CVKM_MULADD_OPERATIONS(ulvec4, uint64_t, b),\
  CVKM_MULADD_OPERATIONS(vec4, float, b),\
  CVKM_MULADD_OPERATIONS(dvec4, double, b)\
)(a, b, result)

#define CVKM_SCALAR_OPERATION(operation, type, suffix) static type vkm_##operation##suffix(const type x) {\
  return (type)operation((double)x);\
}

#define CVKM_TWO_SCALAR_OPERATION(operation, type, suffix) static type vkm_##operation##suffix(\
  const type a,\
  const type b\
) {\
  return (type)operation((double)a, (double)b);\
}

static float vkm_inverse_sqrtf(const float x) {
  return 1.0f / sqrtf(x);
}

static double vkm_inverse_sqrtd(const double x) {
  return 1.0 / sqrt(x);
}

#define CVKM_MIN_OPERATION(type, suffix) static type vkm_min##suffix(const type a, const type b) {\
  return a < b ? a : b;\
}

#define CVKM_VEC2_MIN_OPERATION(type) static void type##_min(const type* a, const type* b, type* result) {\
  *result = (type){ {\
    a->x < b->x ? a->x : b->x,\
    a->y < b->y ? a->y : b->y,\
  } };\
}

#define CVKM_VEC3_MIN_OPERATION(type) static void type##_min(const type* a, const type* b, type* result) {\
  *result = (type){ {\
    a->x < b->x ? a->x : b->x,\
    a->y < b->y ? a->y : b->y,\
    a->z < b->z ? a->z : b->z,\
  } };\
}

#define CVKM_VEC4_MIN_OPERATION(type) static void type##_min(const type* a, const type* b, type* result) {\
  *result = (type){ {\
    a->x < b->x ? a->x : b->x,\
    a->y < b->y ? a->y : b->y,\
    a->z < b->z ? a->z : b->z,\
    a->w < b->w ? a->w : b->w,\
  } };\
}

#define CVKM_VEC2_SCALAR_MIN_OPERATION(type, scalar_type) static scalar_type type##_scalar_min(const type* vec) {\
  return vec->x < vec->y ? vec->x : vec->y;\
}

#define CVKM_VEC3_SCALAR_MIN_OPERATION(type, scalar_type) static scalar_type type##_scalar_min(const type* vec) {\
  const scalar_type min_xy = vec->x < vec->y ? vec->x : vec->y;\
  return min_xy < vec->z ? min_xy : vec->z;\
}

#define CVKM_VEC4_SCALAR_MIN_OPERATION(type, scalar_type) static scalar_type type##_scalar_min(const type* vec) {\
  const scalar_type min_xy = vec->x < vec->y ? vec->x : vec->y;\
  const scalar_type min_zw = vec->z < vec->w ? vec->z : vec->w;\
  return min_xy < min_zw ? min_xy : min_zw;\
}

#define CVKM_MAX_OPERATION(type, suffix) static type vkm_max##suffix(const type a, const type b) {\
  return a > b ? a : b;\
}

#define CVKM_VEC2_MAX_OPERATION(type) static void type##_max(const type* a, const type* b, type* result) {\
  *result = (type){ {\
    a->x > b->x ? a->x : b->x,\
    a->y > b->y ? a->y : b->y,\
  } };\
}

#define CVKM_VEC3_MAX_OPERATION(type) static void type##_max(const type* a, const type* b, type* result) {\
  *result = (type){ {\
    a->x > b->x ? a->x : b->x,\
    a->y > b->y ? a->y : b->y,\
    a->z > b->z ? a->z : b->z,\
  } };\
}

#define CVKM_VEC4_MAX_OPERATION(type) static void type##_max(const type* a, const type* b, type* result) {\
  *result = (type){ {\
    a->x > b->x ? a->x : b->x,\
    a->y > b->y ? a->y : b->y,\
    a->z > b->z ? a->z : b->z,\
    a->w > b->w ? a->w : b->w,\
  } };\
}

#define CVKM_VEC2_SCALAR_MAX_OPERATION(type, scalar_type) static scalar_type type##_scalar_max(const type* vec) {\
  return vec->x > vec->y ? vec->x : vec->y;\
}

#define CVKM_VEC3_SCALAR_MAX_OPERATION(type, scalar_type) static scalar_type type##_scalar_max(const type* vec) {\
  const scalar_type max_xy = vec->x > vec->y ? vec->x : vec->y;\
  return max_xy > vec->z ? max_xy : vec->z;\
}

#define CVKM_VEC4_SCALAR_MAX_OPERATION(type, scalar_type) static scalar_type type##_scalar_max(const type* vec) {\
  const scalar_type max_xy = vec->x > vec->y ? vec->x : vec->y;\
  const scalar_type max_zw = vec->z > vec->w ? vec->z : vec->w;\
  return max_xy > max_zw ? max_xy : max_zw;\
}

#define CVKM_CLAMP_OPERATION(type, suffix) static type vkm_clamp##suffix(\
  const type value,\
  const type min,\
  const type max\
) {\
  return vkm_min##suffix(vkm_max##suffix(value, min), max);\
}\

#define CVKM_INTEGER_ABS_OPERATION(type, bits, suffix) static type vkm_abs##suffix(const type x) {\
  return ((x >> (bits)) ^ x) - 1;\
}

CVKM_SCALAR_OPERATION(sin, int8_t, b)
CVKM_SCALAR_OPERATION(sin, uint8_t, ub)
CVKM_SCALAR_OPERATION(sin, int16_t, s)
CVKM_SCALAR_OPERATION(sin, uint16_t, us)
CVKM_SCALAR_OPERATION(sin, int32_t, i)
CVKM_SCALAR_OPERATION(sin, uint32_t, ui)
CVKM_SCALAR_OPERATION(sin, int64_t, l)
CVKM_SCALAR_OPERATION(sin, uint64_t, ul)

CVKM_SCALAR_OPERATION(cos, int8_t, b)
CVKM_SCALAR_OPERATION(cos, uint8_t, ub)
CVKM_SCALAR_OPERATION(cos, int16_t, s)
CVKM_SCALAR_OPERATION(cos, uint16_t, us)
CVKM_SCALAR_OPERATION(cos, int32_t, i)
CVKM_SCALAR_OPERATION(cos, uint32_t, ui)
CVKM_SCALAR_OPERATION(cos, int64_t, l)
CVKM_SCALAR_OPERATION(cos, uint64_t, ul)

CVKM_SCALAR_OPERATION(tan, int8_t, b)
CVKM_SCALAR_OPERATION(tan, uint8_t, ub)
CVKM_SCALAR_OPERATION(tan, int16_t, s)
CVKM_SCALAR_OPERATION(tan, uint16_t, us)
CVKM_SCALAR_OPERATION(tan, int32_t, i)
CVKM_SCALAR_OPERATION(tan, uint32_t, ui)
CVKM_SCALAR_OPERATION(tan, int64_t, l)
CVKM_SCALAR_OPERATION(tan, uint64_t, ul)

CVKM_SCALAR_OPERATION(asin, int8_t, b)
CVKM_SCALAR_OPERATION(asin, uint8_t, ub)
CVKM_SCALAR_OPERATION(asin, int16_t, s)
CVKM_SCALAR_OPERATION(asin, uint16_t, us)
CVKM_SCALAR_OPERATION(asin, int32_t, i)
CVKM_SCALAR_OPERATION(asin, uint32_t, ui)
CVKM_SCALAR_OPERATION(asin, int64_t, l)
CVKM_SCALAR_OPERATION(asin, uint64_t, ul)

CVKM_SCALAR_OPERATION(acos, int8_t, b)
CVKM_SCALAR_OPERATION(acos, uint8_t, ub)
CVKM_SCALAR_OPERATION(acos, int16_t, s)
CVKM_SCALAR_OPERATION(acos, uint16_t, us)
CVKM_SCALAR_OPERATION(acos, int32_t, i)
CVKM_SCALAR_OPERATION(acos, uint32_t, ui)
CVKM_SCALAR_OPERATION(acos, int64_t, l)
CVKM_SCALAR_OPERATION(acos, uint64_t, ul)

CVKM_SCALAR_OPERATION(atan, int8_t, b)
CVKM_SCALAR_OPERATION(atan, uint8_t, ub)
CVKM_SCALAR_OPERATION(atan, int16_t, s)
CVKM_SCALAR_OPERATION(atan, uint16_t, us)
CVKM_SCALAR_OPERATION(atan, int32_t, i)
CVKM_SCALAR_OPERATION(atan, uint32_t, ui)
CVKM_SCALAR_OPERATION(atan, int64_t, l)
CVKM_SCALAR_OPERATION(atan, uint64_t, ul)

CVKM_TWO_SCALAR_OPERATION(atan2, int8_t, b)
CVKM_TWO_SCALAR_OPERATION(atan2, uint8_t, ub)
CVKM_TWO_SCALAR_OPERATION(atan2, int16_t, s)
CVKM_TWO_SCALAR_OPERATION(atan2, uint16_t, us)
CVKM_TWO_SCALAR_OPERATION(atan2, int32_t, i)
CVKM_TWO_SCALAR_OPERATION(atan2, uint32_t, ui)
CVKM_TWO_SCALAR_OPERATION(atan2, int64_t, l)
CVKM_TWO_SCALAR_OPERATION(atan2, uint64_t, ul)

CVKM_SCALAR_OPERATION(sqrt, int8_t, b)
CVKM_SCALAR_OPERATION(sqrt, uint8_t, ub)
CVKM_SCALAR_OPERATION(sqrt, int16_t, s)
CVKM_SCALAR_OPERATION(sqrt, uint16_t, us)
CVKM_SCALAR_OPERATION(sqrt, int32_t, i)
CVKM_SCALAR_OPERATION(sqrt, uint32_t, ui)
CVKM_SCALAR_OPERATION(sqrt, int64_t, l)
CVKM_SCALAR_OPERATION(sqrt, uint64_t, ul)

CVKM_TWO_SCALAR_OPERATION(pow, int8_t, b)
CVKM_TWO_SCALAR_OPERATION(pow, uint8_t, ub)
CVKM_TWO_SCALAR_OPERATION(pow, int16_t, s)
CVKM_TWO_SCALAR_OPERATION(pow, uint16_t, us)
CVKM_TWO_SCALAR_OPERATION(pow, int32_t, i)
CVKM_TWO_SCALAR_OPERATION(pow, uint32_t, ui)
CVKM_TWO_SCALAR_OPERATION(pow, int64_t, l)
CVKM_TWO_SCALAR_OPERATION(pow, uint64_t, ul)

CVKM_MIN_OPERATION(int8_t, b)
CVKM_MIN_OPERATION(uint8_t, ub)
CVKM_MIN_OPERATION(int16_t, s)
CVKM_MIN_OPERATION(uint16_t, us)
CVKM_MIN_OPERATION(int32_t, i)
CVKM_MIN_OPERATION(uint32_t, ui)
CVKM_MIN_OPERATION(int64_t, l)
CVKM_MIN_OPERATION(uint64_t, ul)
CVKM_MIN_OPERATION(float, f)
CVKM_MIN_OPERATION(double,)

CVKM_VEC2_MIN_OPERATION(vkm_bvec2)
CVKM_VEC2_MIN_OPERATION(vkm_ubvec2)
CVKM_VEC2_MIN_OPERATION(vkm_svec2)
CVKM_VEC2_MIN_OPERATION(vkm_usvec2)
CVKM_VEC2_MIN_OPERATION(vkm_ivec2)
CVKM_VEC2_MIN_OPERATION(vkm_uvec2)
CVKM_VEC2_MIN_OPERATION(vkm_lvec2)
CVKM_VEC2_MIN_OPERATION(vkm_ulvec2)
CVKM_VEC2_MIN_OPERATION(vkm_vec2)
CVKM_VEC2_MIN_OPERATION(vkm_dvec2)

CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_bvec2, int8_t)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_ubvec2, uint8_t)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_svec2, int16_t)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_usvec2, uint16_t)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_ivec2, int32_t)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_uvec2, uint32_t)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_lvec2, int64_t)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_ulvec2, uint64_t)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_vec2, float)
CVKM_VEC2_SCALAR_MIN_OPERATION(vkm_dvec2, double)

CVKM_VEC3_MIN_OPERATION(vkm_bvec3)
CVKM_VEC3_MIN_OPERATION(vkm_ubvec3)
CVKM_VEC3_MIN_OPERATION(vkm_svec3)
CVKM_VEC3_MIN_OPERATION(vkm_usvec3)
CVKM_VEC3_MIN_OPERATION(vkm_ivec3)
CVKM_VEC3_MIN_OPERATION(vkm_uvec3)
CVKM_VEC3_MIN_OPERATION(vkm_lvec3)
CVKM_VEC3_MIN_OPERATION(vkm_ulvec3)
CVKM_VEC3_MIN_OPERATION(vkm_vec3)
CVKM_VEC3_MIN_OPERATION(vkm_dvec3)

CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_bvec3, int8_t)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_ubvec3, uint8_t)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_svec3, int16_t)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_usvec3, uint16_t)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_ivec3, int32_t)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_uvec3, uint32_t)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_lvec3, int64_t)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_ulvec3, uint64_t)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_vec3, float)
CVKM_VEC3_SCALAR_MIN_OPERATION(vkm_dvec3, double)

CVKM_VEC4_MIN_OPERATION(vkm_bvec4)
CVKM_VEC4_MIN_OPERATION(vkm_ubvec4)
CVKM_VEC4_MIN_OPERATION(vkm_svec4)
CVKM_VEC4_MIN_OPERATION(vkm_usvec4)
CVKM_VEC4_MIN_OPERATION(vkm_ivec4)
CVKM_VEC4_MIN_OPERATION(vkm_uvec4)
CVKM_VEC4_MIN_OPERATION(vkm_lvec4)
CVKM_VEC4_MIN_OPERATION(vkm_ulvec4)
CVKM_VEC4_MIN_OPERATION(vkm_vec4)
CVKM_VEC4_MIN_OPERATION(vkm_dvec4)
CVKM_VEC4_MIN_OPERATION(vkm_quat)

CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_bvec4, int8_t)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_ubvec4, uint8_t)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_svec4, int16_t)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_usvec4, uint16_t)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_ivec4, int32_t)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_uvec4, uint32_t)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_lvec4, int64_t)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_ulvec4, uint64_t)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_vec4, float)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_dvec4, double)
CVKM_VEC4_SCALAR_MIN_OPERATION(vkm_quat, float)

CVKM_MAX_OPERATION(int8_t, b)
CVKM_MAX_OPERATION(uint8_t, ub)
CVKM_MAX_OPERATION(int16_t, s)
CVKM_MAX_OPERATION(uint16_t, us)
CVKM_MAX_OPERATION(int32_t, i)
CVKM_MAX_OPERATION(uint32_t, ui)
CVKM_MAX_OPERATION(int64_t, l)
CVKM_MAX_OPERATION(uint64_t, ul)
CVKM_MAX_OPERATION(float, f)
CVKM_MAX_OPERATION(double,)

CVKM_VEC2_MAX_OPERATION(vkm_bvec2)
CVKM_VEC2_MAX_OPERATION(vkm_ubvec2)
CVKM_VEC2_MAX_OPERATION(vkm_svec2)
CVKM_VEC2_MAX_OPERATION(vkm_usvec2)
CVKM_VEC2_MAX_OPERATION(vkm_ivec2)
CVKM_VEC2_MAX_OPERATION(vkm_uvec2)
CVKM_VEC2_MAX_OPERATION(vkm_lvec2)
CVKM_VEC2_MAX_OPERATION(vkm_ulvec2)
CVKM_VEC2_MAX_OPERATION(vkm_vec2)
CVKM_VEC2_MAX_OPERATION(vkm_dvec2)

CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_bvec2, int8_t)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_ubvec2, uint8_t)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_svec2, int16_t)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_usvec2, uint16_t)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_ivec2, int32_t)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_uvec2, uint32_t)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_lvec2, int64_t)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_ulvec2, uint64_t)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_vec2, float)
CVKM_VEC2_SCALAR_MAX_OPERATION(vkm_dvec2, double)

CVKM_VEC3_MAX_OPERATION(vkm_bvec3)
CVKM_VEC3_MAX_OPERATION(vkm_ubvec3)
CVKM_VEC3_MAX_OPERATION(vkm_svec3)
CVKM_VEC3_MAX_OPERATION(vkm_usvec3)
CVKM_VEC3_MAX_OPERATION(vkm_ivec3)
CVKM_VEC3_MAX_OPERATION(vkm_uvec3)
CVKM_VEC3_MAX_OPERATION(vkm_lvec3)
CVKM_VEC3_MAX_OPERATION(vkm_ulvec3)
CVKM_VEC3_MAX_OPERATION(vkm_vec3)
CVKM_VEC3_MAX_OPERATION(vkm_dvec3)

CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_bvec3, int8_t)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_ubvec3, uint8_t)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_svec3, int16_t)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_usvec3, uint16_t)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_ivec3, int32_t)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_uvec3, uint32_t)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_lvec3, int64_t)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_ulvec3, uint64_t)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_vec3, float)
CVKM_VEC3_SCALAR_MAX_OPERATION(vkm_dvec3, double)

CVKM_VEC4_MAX_OPERATION(vkm_bvec4)
CVKM_VEC4_MAX_OPERATION(vkm_ubvec4)
CVKM_VEC4_MAX_OPERATION(vkm_svec4)
CVKM_VEC4_MAX_OPERATION(vkm_usvec4)
CVKM_VEC4_MAX_OPERATION(vkm_ivec4)
CVKM_VEC4_MAX_OPERATION(vkm_uvec4)
CVKM_VEC4_MAX_OPERATION(vkm_lvec4)
CVKM_VEC4_MAX_OPERATION(vkm_ulvec4)
CVKM_VEC4_MAX_OPERATION(vkm_vec4)
CVKM_VEC4_MAX_OPERATION(vkm_dvec4)
CVKM_VEC4_MAX_OPERATION(vkm_quat)

CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_bvec4, int8_t)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_ubvec4, uint8_t)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_svec4, int16_t)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_usvec4, uint16_t)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_ivec4, int32_t)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_uvec4, uint32_t)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_lvec4, int64_t)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_ulvec4, uint64_t)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_vec4, float)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_dvec4, double)
CVKM_VEC4_SCALAR_MAX_OPERATION(vkm_quat, float)

CVKM_CLAMP_OPERATION(int8_t, b)
CVKM_CLAMP_OPERATION(uint8_t, ub)
CVKM_CLAMP_OPERATION(int16_t, s)
CVKM_CLAMP_OPERATION(uint16_t, us)
CVKM_CLAMP_OPERATION(int32_t, i)
CVKM_CLAMP_OPERATION(uint32_t, ui)
CVKM_CLAMP_OPERATION(int64_t, l)
CVKM_CLAMP_OPERATION(uint64_t, ul)
CVKM_CLAMP_OPERATION(float, f)
CVKM_CLAMP_OPERATION(double, )

CVKM_INTEGER_ABS_OPERATION(int8_t, 7, b)
CVKM_INTEGER_ABS_OPERATION(int16_t, 15, s)
CVKM_INTEGER_ABS_OPERATION(int32_t, 31, i)
CVKM_INTEGER_ABS_OPERATION(int64_t, 63, l)
#define cvkm_absf fabsf
#define cvkm_abs fabs

#define vkm_sin(x) _Generic(x,\
  int8_t: vkm_sinb,\
  uint8_t: vkm_sinub,\
  int16_t: vkm_sins,\
  uint16_t: vkm_sinus,\
  int32_t: vkm_sini,\
  uint32_t: vkm_sinui,\
  int64_t: vkm_sinl,\
  uint64_t: vkm_sinul,\
  float: sinf,\
  double: sin,\
  const int8_t: vkm_sinb,\
  const uint8_t: vkm_sinub,\
  const int16_t: vkm_sins,\
  const uint16_t: vkm_sinus,\
  const int32_t: vkm_sini,\
  const uint32_t: vkm_sinui,\
  const int64_t: vkm_sinl,\
  const uint64_t: vkm_sinul,\
  const float: sinf,\
  const double: sin\
)(x)

#define vkm_cos(x) _Generic(x,\
  int8_t: vkm_cosb,\
  uint8_t: vkm_cosub,\
  int16_t: vkm_coss,\
  uint16_t: vkm_cosus,\
  int32_t: vkm_cosi,\
  uint32_t: vkm_cosui,\
  int64_t: vkm_cosl,\
  uint64_t: vkm_cosul,\
  float: cosf,\
  double: cos,\
  const int8_t: vkm_cosb,\
  const uint8_t: vkm_cosub,\
  const int16_t: vkm_coss,\
  const uint16_t: vkm_cosus,\
  const int32_t: vkm_cosi,\
  const uint32_t: vkm_cosui,\
  const int64_t: vkm_cosl,\
  const uint64_t: vkm_cosul,\
  const float: cosf,\
  const double: cos\
)(x)

#define vkm_tan(x) _Generic(x,\
  int8_t: vkm_tanb,\
  uint8_t: vkm_tanub,\
  int16_t: vkm_tans,\
  uint16_t: vkm_tanus,\
  int32_t: vkm_tani,\
  uint32_t: vkm_tanui,\
  int64_t: vkm_tanl,\
  uint64_t: vkm_tanul,\
  float: tanf,\
  double: tan,\
  const int8_t: vkm_tanb,\
  const uint8_t: vkm_tanub,\
  const int16_t: vkm_tans,\
  const uint16_t: vkm_tanus,\
  const int32_t: vkm_tani,\
  const uint32_t: vkm_tanui,\
  const int64_t: vkm_tanl,\
  const uint64_t: vkm_tanul,\
  const float: tanf,\
  const double: tan\
)(x)

#define vkm_asin(x) _Generic(x,\
  int8_t: vkm_asinb,\
  uint8_t: vkm_asinub,\
  int16_t: vkm_asins,\
  uint16_t: vkm_asinus,\
  int32_t: vkm_asini,\
  uint32_t: vkm_asinui,\
  int64_t: vkm_asinl,\
  uint64_t: vkm_asinul,\
  float: asinf,\
  double: asin,\
  const int8_t: vkm_asinb,\
  const uint8_t: vkm_asinub,\
  const int16_t: vkm_asins,\
  const uint16_t: vkm_asinus,\
  const int32_t: vkm_asini,\
  const uint32_t: vkm_asinui,\
  const int64_t: vkm_asinl,\
  const uint64_t: vkm_asinul,\
  const float: asinf,\
  const double: asin\
)(x)

#define vkm_acos(x) _Generic(x,\
  int8_t: vkm_acosb,\
  uint8_t: vkm_acosub,\
  int16_t: vkm_acoss,\
  uint16_t: vkm_acosus,\
  int32_t: vkm_acosi,\
  uint32_t: vkm_acosui,\
  int64_t: vkm_acosl,\
  uint64_t: vkm_acosul,\
  float: acosf,\
  double: acos,\
  const int8_t: vkm_acosb,\
  const uint8_t: vkm_acosub,\
  const int16_t: vkm_acoss,\
  const uint16_t: vkm_acosus,\
  const int32_t: vkm_acosi,\
  const uint32_t: vkm_acosui,\
  const int64_t: vkm_acosl,\
  const uint64_t: vkm_acosul,\
  const float: acosf,\
  const double: acos\
)(x)

#define vkm_atan(x) _Generic(x,\
  int8_t: vkm_atanb,\
  uint8_t: vkm_atanub,\
  int16_t: vkm_atans,\
  uint16_t: vkm_atanus,\
  int32_t: vkm_atani,\
  uint32_t: vkm_atanui,\
  int64_t: vkm_atanl,\
  uint64_t: vkm_atanul,\
  float: atanf,\
  double: atan,\
  const int8_t: vkm_atanb,\
  const uint8_t: vkm_atanub,\
  const int16_t: vkm_atans,\
  const uint16_t: vkm_atanus,\
  const int32_t: vkm_atani,\
  const uint32_t: vkm_atanui,\
  const int64_t: vkm_atanl,\
  const uint64_t: vkm_atanul,\
  const float: atanf,\
  const double: atan\
)(x)

#define vkm_atan2(a, ...) _Generic(a,\
  int8_t: vkm_atan2b,\
  uint8_t: vkm_atan2ub,\
  int16_t: vkm_atan2s,\
  uint16_t: vkm_atan2us,\
  int32_t: vkm_atan2i,\
  uint32_t: vkm_atan2ui,\
  int64_t: vkm_atan2l,\
  uint64_t: vkm_atan2ul,\
  float: atan2f,\
  double: atan2,\
  const int8_t: vkm_atan2b,\
  const uint8_t: vkm_atan2ub,\
  const int16_t: vkm_atan2s,\
  const uint16_t: vkm_atan2us,\
  const int32_t: vkm_atan2i,\
  const uint32_t: vkm_atan2ui,\
  const int64_t: vkm_atan2l,\
  const uint64_t: vkm_atan2ul,\
  const float: atan2f,\
  const double: atan2\
)(a, __VA_ARGS__)

#define vkm_sqrt(x) _Generic(x,\
  int8_t: vkm_sqrtb,\
  uint8_t: vkm_sqrtub,\
  int16_t: vkm_sqrts,\
  uint16_t: vkm_sqrtus,\
  int32_t: vkm_sqrti,\
  uint32_t: vkm_sqrtui,\
  int64_t: vkm_sqrtl,\
  uint64_t: vkm_sqrtul,\
  float: sqrtf,\
  double: sqrt,\
  const int8_t: vkm_sqrtb,\
  const uint8_t: vkm_sqrtub,\
  const int16_t: vkm_sqrts,\
  const uint16_t: vkm_sqrtus,\
  const int32_t: vkm_sqrti,\
  const uint32_t: vkm_sqrtui,\
  const int64_t: vkm_sqrtl,\
  const uint64_t: vkm_sqrtul,\
  const float: sqrtf,\
  const double: sqrt\
)(x)

#define vkm_min(a, ...) _Generic(a,\
  int8_t: vkm_minb,\
  uint8_t: vkm_minub,\
  int16_t: vkm_mins,\
  uint16_t: vkm_minus,\
  int32_t: vkm_mini,\
  uint32_t: vkm_minui,\
  int64_t: vkm_minl,\
  uint64_t: vkm_minul,\
  float: vkm_minf,\
  double: vkm_min,\
  vkm_bvec2*: vkm_bvec2_min,\
  vkm_ubvec2*: vkm_ubvec2_min,\
  vkm_svec2*: vkm_svec2_min,\
  vkm_usvec2*: vkm_usvec2_min,\
  vkm_ivec2*: vkm_ivec2_min,\
  vkm_uvec2*: vkm_uvec2_min,\
  vkm_lvec2*: vkm_lvec2_min,\
  vkm_ulvec2*: vkm_ulvec2_min,\
  vkm_vec2*: vkm_vec2_min,\
  vkm_dvec2*: vkm_dvec2_min,\
  vkm_bvec3*: vkm_bvec3_min,\
  vkm_ubvec3*: vkm_ubvec3_min,\
  vkm_svec3*: vkm_svec3_min,\
  vkm_usvec3*: vkm_usvec3_min,\
  vkm_ivec3*: vkm_ivec3_min,\
  vkm_uvec3*: vkm_uvec3_min,\
  vkm_lvec3*: vkm_lvec3_min,\
  vkm_ulvec3*: vkm_ulvec3_min,\
  vkm_vec3*: vkm_vec3_min,\
  vkm_dvec3*: vkm_dvec3_min,\
  vkm_bvec4*: vkm_bvec4_min,\
  vkm_ubvec4*: vkm_ubvec4_min,\
  vkm_svec4*: vkm_svec4_min,\
  vkm_usvec4*: vkm_usvec4_min,\
  vkm_ivec4*: vkm_ivec4_min,\
  vkm_uvec4*: vkm_uvec4_min,\
  vkm_lvec4*: vkm_lvec4_min,\
  vkm_ulvec4*: vkm_ulvec4_min,\
  vkm_vec4*: vkm_vec4_min,\
  vkm_dvec4*: vkm_dvec4_min,\
  vkm_quat*: vkm_quat_min,\
  const int8_t: vkm_minb,\
  const uint8_t: vkm_minub,\
  const int16_t: vkm_mins,\
  const uint16_t: vkm_minus,\
  const int32_t: vkm_mini,\
  const uint32_t: vkm_minui,\
  const int64_t: vkm_minl,\
  const uint64_t: vkm_minul,\
  const float: vkm_minf,\
  const double: vkm_min,\
  const vkm_bvec2*: vkm_bvec2_min,\
  const vkm_ubvec2*: vkm_ubvec2_min,\
  const vkm_svec2*: vkm_svec2_min,\
  const vkm_usvec2*: vkm_usvec2_min,\
  const vkm_ivec2*: vkm_ivec2_min,\
  const vkm_uvec2*: vkm_uvec2_min,\
  const vkm_lvec2*: vkm_lvec2_min,\
  const vkm_ulvec2*: vkm_ulvec2_min,\
  const vkm_vec2*: vkm_vec2_min,\
  const vkm_dvec2*: vkm_dvec2_min,\
  const vkm_bvec3*: vkm_bvec3_min,\
  const vkm_ubvec3*: vkm_ubvec3_min,\
  const vkm_svec3*: vkm_svec3_min,\
  const vkm_usvec3*: vkm_usvec3_min,\
  const vkm_ivec3*: vkm_ivec3_min,\
  const vkm_uvec3*: vkm_uvec3_min,\
  const vkm_lvec3*: vkm_lvec3_min,\
  const vkm_ulvec3*: vkm_ulvec3_min,\
  const vkm_vec3*: vkm_vec3_min,\
  const vkm_dvec3*: vkm_dvec3_min,\
  const vkm_bvec4*: vkm_bvec4_min,\
  const vkm_ubvec4*: vkm_ubvec4_min,\
  const vkm_svec4*: vkm_svec4_min,\
  const vkm_usvec4*: vkm_usvec4_min,\
  const vkm_ivec4*: vkm_ivec4_min,\
  const vkm_uvec4*: vkm_uvec4_min,\
  const vkm_lvec4*: vkm_lvec4_min,\
  const vkm_ulvec4*: vkm_ulvec4_min,\
  const vkm_vec4*: vkm_vec4_min,\
  const vkm_dvec4*: vkm_dvec4_min,\
  const vkm_quat*: vkm_quat_min\
)(a, __VA_ARGS__)

#define vkm_scalar_min(a) _Generic(a,\
  vkm_bvec2*: vkm_bvec2_scalar_min,\
  vkm_ubvec2*: vkm_ubvec2_scalar_min,\
  vkm_svec2*: vkm_svec2_scalar_min,\
  vkm_usvec2*: vkm_usvec2_scalar_min,\
  vkm_ivec2*: vkm_ivec2_scalar_min,\
  vkm_uvec2*: vkm_uvec2_scalar_min,\
  vkm_lvec2*: vkm_lvec2_scalar_min,\
  vkm_ulvec2*: vkm_ulvec2_scalar_min,\
  vkm_vec2*: vkm_vec2_scalar_min,\
  vkm_dvec2*: vkm_dvec2_scalar_min,\
  vkm_bvec3*: vkm_bvec3_scalar_min,\
  vkm_ubvec3*: vkm_ubvec3_scalar_min,\
  vkm_svec3*: vkm_svec3_scalar_min,\
  vkm_usvec3*: vkm_usvec3_scalar_min,\
  vkm_ivec3*: vkm_ivec3_scalar_min,\
  vkm_uvec3*: vkm_uvec3_scalar_min,\
  vkm_lvec3*: vkm_lvec3_scalar_min,\
  vkm_ulvec3*: vkm_ulvec3_scalar_min,\
  vkm_vec3*: vkm_vec3_scalar_min,\
  vkm_dvec3*: vkm_dvec3_scalar_min,\
  vkm_bvec4*: vkm_bvec4_scalar_min,\
  vkm_ubvec4*: vkm_ubvec4_scalar_min,\
  vkm_svec4*: vkm_svec4_scalar_min,\
  vkm_usvec4*: vkm_usvec4_scalar_min,\
  vkm_ivec4*: vkm_ivec4_scalar_min,\
  vkm_uvec4*: vkm_uvec4_scalar_min,\
  vkm_lvec4*: vkm_lvec4_scalar_min,\
  vkm_ulvec4*: vkm_ulvec4_scalar_min,\
  vkm_vec4*: vkm_vec4_scalar_min,\
  vkm_dvec4*: vkm_dvec4_scalar_min,\
  vkm_quat*: vkm_quat_scalar_min,\
  const vkm_bvec2*: vkm_bvec2_scalar_min,\
  const vkm_ubvec2*: vkm_ubvec2_scalar_min,\
  const vkm_svec2*: vkm_svec2_scalar_min,\
  const vkm_usvec2*: vkm_usvec2_scalar_min,\
  const vkm_ivec2*: vkm_ivec2_scalar_min,\
  const vkm_uvec2*: vkm_uvec2_scalar_min,\
  const vkm_lvec2*: vkm_lvec2_scalar_min,\
  const vkm_ulvec2*: vkm_ulvec2_scalar_min,\
  const vkm_vec2*: vkm_vec2_scalar_min,\
  const vkm_dvec2*: vkm_dvec2_scalar_min,\
  const vkm_bvec3*: vkm_bvec3_scalar_min,\
  const vkm_ubvec3*: vkm_ubvec3_scalar_min,\
  const vkm_svec3*: vkm_svec3_scalar_min,\
  const vkm_usvec3*: vkm_usvec3_scalar_min,\
  const vkm_ivec3*: vkm_ivec3_scalar_min,\
  const vkm_uvec3*: vkm_uvec3_scalar_min,\
  const vkm_lvec3*: vkm_lvec3_scalar_min,\
  const vkm_ulvec3*: vkm_ulvec3_scalar_min,\
  const vkm_vec3*: vkm_vec3_scalar_min,\
  const vkm_dvec3*: vkm_dvec3_scalar_min,\
  const vkm_bvec4*: vkm_bvec4_scalar_min,\
  const vkm_ubvec4*: vkm_ubvec4_scalar_min,\
  const vkm_svec4*: vkm_svec4_scalar_min,\
  const vkm_usvec4*: vkm_usvec4_scalar_min,\
  const vkm_ivec4*: vkm_ivec4_scalar_min,\
  const vkm_uvec4*: vkm_uvec4_scalar_min,\
  const vkm_lvec4*: vkm_lvec4_scalar_min,\
  const vkm_ulvec4*: vkm_ulvec4_scalar_min,\
  const vkm_vec4*: vkm_vec4_scalar_min,\
  const vkm_dvec4*: vkm_dvec4_scalar_min,\
  const vkm_quat*: vkm_quat_scalar_min\
)(a)

#define vkm_max(a, ...) _Generic(a,\
  int8_t: vkm_maxb,\
  uint8_t: vkm_maxub,\
  int16_t: vkm_maxs,\
  uint16_t: vkm_maxus,\
  int32_t: vkm_maxi,\
  uint32_t: vkm_maxui,\
  int64_t: vkm_maxl,\
  uint64_t: vkm_maxul,\
  float: vkm_maxf,\
  double: vkm_max,\
  vkm_bvec2*: vkm_bvec2_max,\
  vkm_ubvec2*: vkm_ubvec2_max,\
  vkm_svec2*: vkm_svec2_max,\
  vkm_usvec2*: vkm_usvec2_max,\
  vkm_ivec2*: vkm_ivec2_max,\
  vkm_uvec2*: vkm_uvec2_max,\
  vkm_lvec2*: vkm_lvec2_max,\
  vkm_ulvec2*: vkm_ulvec2_max,\
  vkm_vec2*: vkm_vec2_max,\
  vkm_dvec2*: vkm_dvec2_max,\
  vkm_bvec3*: vkm_bvec3_max,\
  vkm_ubvec3*: vkm_ubvec3_max,\
  vkm_svec3*: vkm_svec3_max,\
  vkm_usvec3*: vkm_usvec3_max,\
  vkm_ivec3*: vkm_ivec3_max,\
  vkm_uvec3*: vkm_uvec3_max,\
  vkm_lvec3*: vkm_lvec3_max,\
  vkm_ulvec3*: vkm_ulvec3_max,\
  vkm_vec3*: vkm_vec3_max,\
  vkm_dvec3*: vkm_dvec3_max,\
  vkm_bvec4*: vkm_bvec4_max,\
  vkm_ubvec4*: vkm_ubvec4_max,\
  vkm_svec4*: vkm_svec4_max,\
  vkm_usvec4*: vkm_usvec4_max,\
  vkm_ivec4*: vkm_ivec4_max,\
  vkm_uvec4*: vkm_uvec4_max,\
  vkm_lvec4*: vkm_lvec4_max,\
  vkm_ulvec4*: vkm_ulvec4_max,\
  vkm_vec4*: vkm_vec4_max,\
  vkm_dvec4*: vkm_dvec4_max,\
  vkm_quat*: vkm_quat_max,\
  const int8_t: vkm_maxb,\
  const uint8_t: vkm_maxub,\
  const int16_t: vkm_maxs,\
  const uint16_t: vkm_maxus,\
  const int32_t: vkm_maxi,\
  const uint32_t: vkm_maxui,\
  const int64_t: vkm_maxl,\
  const uint64_t: vkm_maxul,\
  const float: vkm_maxf,\
  const double: vkm_max,\
  const vkm_bvec2*: vkm_bvec2_max,\
  const vkm_ubvec2*: vkm_ubvec2_max,\
  const vkm_svec2*: vkm_svec2_max,\
  const vkm_usvec2*: vkm_usvec2_max,\
  const vkm_ivec2*: vkm_ivec2_max,\
  const vkm_uvec2*: vkm_uvec2_max,\
  const vkm_lvec2*: vkm_lvec2_max,\
  const vkm_ulvec2*: vkm_ulvec2_max,\
  const vkm_vec2*: vkm_vec2_max,\
  const vkm_dvec2*: vkm_dvec2_max,\
  const vkm_bvec3*: vkm_bvec3_max,\
  const vkm_ubvec3*: vkm_ubvec3_max,\
  const vkm_svec3*: vkm_svec3_max,\
  const vkm_usvec3*: vkm_usvec3_max,\
  const vkm_ivec3*: vkm_ivec3_max,\
  const vkm_uvec3*: vkm_uvec3_max,\
  const vkm_lvec3*: vkm_lvec3_max,\
  const vkm_ulvec3*: vkm_ulvec3_max,\
  const vkm_vec3*: vkm_vec3_max,\
  const vkm_dvec3*: vkm_dvec3_max,\
  const vkm_bvec4*: vkm_bvec4_max,\
  const vkm_ubvec4*: vkm_ubvec4_max,\
  const vkm_svec4*: vkm_svec4_max,\
  const vkm_usvec4*: vkm_usvec4_max,\
  const vkm_ivec4*: vkm_ivec4_max,\
  const vkm_uvec4*: vkm_uvec4_max,\
  const vkm_lvec4*: vkm_lvec4_max,\
  const vkm_ulvec4*: vkm_ulvec4_max,\
  const vkm_vec4*: vkm_vec4_max,\
  const vkm_dvec4*: vkm_dvec4_max,\
  const vkm_quat*: vkm_quat_max\
)(a, __VA_ARGS__)

#define vkm_scalar_max(a) _Generic(a,\
  vkm_bvec2*: vkm_bvec2_scalar_max,\
  vkm_ubvec2*: vkm_ubvec2_scalar_max,\
  vkm_svec2*: vkm_svec2_scalar_max,\
  vkm_usvec2*: vkm_usvec2_scalar_max,\
  vkm_ivec2*: vkm_ivec2_scalar_max,\
  vkm_uvec2*: vkm_uvec2_scalar_max,\
  vkm_lvec2*: vkm_lvec2_scalar_max,\
  vkm_ulvec2*: vkm_ulvec2_scalar_max,\
  vkm_vec2*: vkm_vec2_scalar_max,\
  vkm_dvec2*: vkm_dvec2_scalar_max,\
  vkm_bvec3*: vkm_bvec3_scalar_max,\
  vkm_ubvec3*: vkm_ubvec3_scalar_max,\
  vkm_svec3*: vkm_svec3_scalar_max,\
  vkm_usvec3*: vkm_usvec3_scalar_max,\
  vkm_ivec3*: vkm_ivec3_scalar_max,\
  vkm_uvec3*: vkm_uvec3_scalar_max,\
  vkm_lvec3*: vkm_lvec3_scalar_max,\
  vkm_ulvec3*: vkm_ulvec3_scalar_max,\
  vkm_vec3*: vkm_vec3_scalar_max,\
  vkm_dvec3*: vkm_dvec3_scalar_max,\
  vkm_bvec4*: vkm_bvec4_scalar_max,\
  vkm_ubvec4*: vkm_ubvec4_scalar_max,\
  vkm_svec4*: vkm_svec4_scalar_max,\
  vkm_usvec4*: vkm_usvec4_scalar_max,\
  vkm_ivec4*: vkm_ivec4_scalar_max,\
  vkm_uvec4*: vkm_uvec4_scalar_max,\
  vkm_lvec4*: vkm_lvec4_scalar_max,\
  vkm_ulvec4*: vkm_ulvec4_scalar_max,\
  vkm_vec4*: vkm_vec4_scalar_max,\
  vkm_dvec4*: vkm_dvec4_scalar_max,\
  vkm_quat*: vkm_quat_scalar_max,\
  const vkm_bvec2*: vkm_bvec2_scalar_max,\
  const vkm_ubvec2*: vkm_ubvec2_scalar_max,\
  const vkm_svec2*: vkm_svec2_scalar_max,\
  const vkm_usvec2*: vkm_usvec2_scalar_max,\
  const vkm_ivec2*: vkm_ivec2_scalar_max,\
  const vkm_uvec2*: vkm_uvec2_scalar_max,\
  const vkm_lvec2*: vkm_lvec2_scalar_max,\
  const vkm_ulvec2*: vkm_ulvec2_scalar_max,\
  const vkm_vec2*: vkm_vec2_scalar_max,\
  const vkm_dvec2*: vkm_dvec2_scalar_max,\
  const vkm_bvec3*: vkm_bvec3_scalar_max,\
  const vkm_ubvec3*: vkm_ubvec3_scalar_max,\
  const vkm_svec3*: vkm_svec3_scalar_max,\
  const vkm_usvec3*: vkm_usvec3_scalar_max,\
  const vkm_ivec3*: vkm_ivec3_scalar_max,\
  const vkm_uvec3*: vkm_uvec3_scalar_max,\
  const vkm_lvec3*: vkm_lvec3_scalar_max,\
  const vkm_ulvec3*: vkm_ulvec3_scalar_max,\
  const vkm_vec3*: vkm_vec3_scalar_max,\
  const vkm_dvec3*: vkm_dvec3_scalar_max,\
  const vkm_bvec4*: vkm_bvec4_scalar_max,\
  const vkm_ubvec4*: vkm_ubvec4_scalar_max,\
  const vkm_svec4*: vkm_svec4_scalar_max,\
  const vkm_usvec4*: vkm_usvec4_scalar_max,\
  const vkm_ivec4*: vkm_ivec4_scalar_max,\
  const vkm_uvec4*: vkm_uvec4_scalar_max,\
  const vkm_lvec4*: vkm_lvec4_scalar_max,\
  const vkm_ulvec4*: vkm_ulvec4_scalar_max,\
  const vkm_vec4*: vkm_vec4_scalar_max,\
  const vkm_dvec4*: vkm_dvec4_scalar_max,\
  const vkm_quat*: vkm_quat_scalar_max\
)(a)

#define vkm_clamp(value, min, max) _Generic(value,\
  int8_t: vkm_clampb,\
  uint8_t: vkm_clampub,\
  int16_t: vkm_clamps,\
  uint16_t: vkm_clampus,\
  int32_t: vkm_clampi,\
  uint32_t: vkm_clampui,\
  int64_t: vkm_clampl,\
  uint64_t: vkm_clampul,\
  float: vkm_clampf,\
  double: vkm_clamp,\
  const int8_t: vkm_clampb,\
  const uint8_t: vkm_clampub,\
  const int16_t: vkm_clamps,\
  const uint16_t: vkm_clampus,\
  const int32_t: vkm_clampi,\
  const uint32_t: vkm_clampui,\
  const int64_t: vkm_clampl,\
  const uint64_t: vkm_clampul,\
  const float: vkm_clampf,\
  const double: vkm_clamp\
)(value, min, max)

#define vkm_abs(value) _Generic(value,\
  int8_t: vkm_absb,\
  int16_t: vkm_abss,\
  int32_t: vkm_absi,\
  int64_t: vkm_absl,\
  float: fabsf,\
  double: fabs,\
  const int8_t: vkm_absb,\
  const int16_t: vkm_abss,\
  const int32_t: vkm_absi,\
  const int64_t: vkm_absl,\
  const float: fabsf,\
  const double: fabs\
)(value)

#define CVKM_CONSTEXPR_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CVKM_CONSTEXPR_MAX(a, b) ((a) > (b) ? (a) : (b))

#define vkm_inverse_sqrt(x) _Generic(x,\
  float: vkm_inverse_sqrtf,\
  double: vkm_inverse_sqrtd\
)(x)

#define vkm_pow(x, ...) _Generic(x,\
  int8_t: vkm_powb,\
  uint8_t: vkm_powub,\
  int16_t: vkm_pows,\
  uint16_t: vkm_powus,\
  int32_t: vkm_powi,\
  uint32_t: vkm_powui,\
  int64_t: vkm_powl,\
  uint64_t: vkm_powul,\
  float: powf,\
  double: pow,\
  const int8_t: vkm_powb,\
  const uint8_t: vkm_powub,\
  const int16_t: vkm_pows,\
  const uint16_t: vkm_powus,\
  const int32_t: vkm_powi,\
  const uint32_t: vkm_powui,\
  const int64_t: vkm_powl,\
  const uint64_t: vkm_powul,\
  const float: powf,\
  const double: pow\
)(x, __VA_ARGS__)

#define vkm_mod(x, ...) _Generic(x,\
    float: fmodf(x, __VA_ARGS__),\
    double: fmod(x, __VA_ARGS__),\
    const float: fmodf(x, __VA_ARGS__),\
    const double: fmod(x, __VA_ARGS__)\
)

#define CVKM_VEC2_MISC_OPERATIONS_FOR_UNSIGNED_INTS(vec_type, scalar_type) static scalar_type vkm_##vec_type##_dot(\
  const vkm_##vec_type* a,\
  const vkm_##vec_type* b\
) {\
  return a->x * b->x + a->y * b->y;\
}\
\
static scalar_type vkm_##vec_type##_sqr_magnitude(const vkm_##vec_type* vec) {\
  return vec->x * vec->x + vec->y * vec->y;\
}\
\
static scalar_type vkm_##vec_type##_magnitude(const vkm_##vec_type* vec) {\
  return vkm_sqrt(vkm_##vec_type##_sqr_magnitude(vec));\
}\
\
static void vkm_##vec_type##_clear(vkm_##vec_type* vec) {\
  *vec = (vkm_##vec_type){ { (scalar_type)0, (scalar_type)0 } };\
}

#define CVKM_VEC2_INVERT(vec_type) static void vkm_##vec_type##_invert(\
  const vkm_##vec_type* vec,\
  vkm_##vec_type* result\
) {\
  *result = (vkm_##vec_type){ { -vec->x, -vec->y } };\
}

#define CVKM_VEC2_NORMALIZE(vec_type, scalar_type) static void vkm_##vec_type##_normalize(\
  const vkm_##vec_type* vec,\
  vkm_##vec_type* result\
) {\
  const scalar_type magnitude = vkm_##vec_type##_magnitude(vec);\
  if (magnitude > (scalar_type)0) {\
    vkm_##vec_type##_div_scalar(vec, magnitude, result);\
  }\
}

#define CVKM_VEC2_MISC_OPERATIONS_FOR_INTS(vec_type, scalar_type)\
  CVKM_VEC2_MISC_OPERATIONS_FOR_UNSIGNED_INTS(vec_type, scalar_type)\
  CVKM_VEC2_INVERT(vec_type)

#define CVKM_VEC2_MISC_OPERATIONS(vec_type, scalar_type)\
  CVKM_VEC2_MISC_OPERATIONS_FOR_INTS(vec_type, scalar_type)\
  CVKM_VEC2_NORMALIZE(vec_type, scalar_type)

CVKM_VEC2_MISC_OPERATIONS_FOR_INTS(bvec2, int8_t)
CVKM_VEC2_MISC_OPERATIONS_FOR_UNSIGNED_INTS(ubvec2, uint8_t)
CVKM_VEC2_MISC_OPERATIONS_FOR_INTS(svec2, int16_t)
CVKM_VEC2_MISC_OPERATIONS_FOR_UNSIGNED_INTS(usvec2, uint16_t)
CVKM_VEC2_MISC_OPERATIONS_FOR_INTS(ivec2, int32_t)
CVKM_VEC2_MISC_OPERATIONS_FOR_UNSIGNED_INTS(uvec2, uint32_t)
CVKM_VEC2_MISC_OPERATIONS_FOR_INTS(lvec2, int64_t)
CVKM_VEC2_MISC_OPERATIONS_FOR_UNSIGNED_INTS(ulvec2, uint64_t)
CVKM_VEC2_MISC_OPERATIONS(vec2, float)
CVKM_VEC2_MISC_OPERATIONS(dvec2, double)

#define CVKM_VEC3_MISC_OPERATIONS_FOR_UNSIGNED_INTS(vec_type, scalar_type) static scalar_type vkm_##vec_type##_dot(\
  const vkm_##vec_type* a,\
  const vkm_##vec_type* b\
) {\
  return a->x * b->x + a->y * b->y + a->z * b->z;\
}\
\
static void vkm_##vec_type##_cross(const vkm_##vec_type* a, const vkm_##vec_type* b, vkm_##vec_type* result){\
  *result = (vkm_##vec_type){ {\
    a->y * b->z - a->z * b->y,\
    a->z * b->x - a->x * b->z,\
    a->x * b->y - a->y * b->x,\
  } };\
}\
\
static scalar_type vkm_##vec_type##_sqr_magnitude(const vkm_##vec_type* vec) {\
  return vec->x * vec->x + vec->y * vec->y + vec->z * vec->z;\
}\
\
static scalar_type vkm_##vec_type##_magnitude(const vkm_##vec_type* vec) {\
  return vkm_sqrt(vkm_##vec_type##_sqr_magnitude(vec));\
}\
\
static void vkm_##vec_type##_clear(vkm_##vec_type* vec) {\
  *vec = (vkm_##vec_type){ { (scalar_type)0, (scalar_type)0, (scalar_type)0 } };\
}

#define CVKM_VEC3_INVERT(vec_type) static void vkm_##vec_type##_invert(\
  const vkm_##vec_type* vec,\
  vkm_##vec_type* result\
) {\
  *result = (vkm_##vec_type){ { -vec->x, -vec->y, -vec->z } };\
}

#define CVKM_VEC3_NORMALIZE(vec_type, scalar_type) static void vkm_##vec_type##_normalize(\
  const vkm_##vec_type* vec,\
  vkm_##vec_type* result\
) {\
  const scalar_type magnitude = vkm_##vec_type##_magnitude(vec);\
  if (magnitude > (scalar_type)0) {\
    vkm_##vec_type##_div_scalar(vec, magnitude, result);\
  }\
}

#define CVKM_VEC3_MISC_OPERATIONS_FOR_INTS(vec_type, scalar_type)\
  CVKM_VEC3_MISC_OPERATIONS_FOR_UNSIGNED_INTS(vec_type, scalar_type)\
  CVKM_VEC3_INVERT(vec_type)

#define CVKM_VEC3_MISC_OPERATIONS(vec_type, scalar_type)\
  CVKM_VEC3_MISC_OPERATIONS_FOR_INTS(vec_type, scalar_type)\
  CVKM_VEC3_NORMALIZE(vec_type, scalar_type)

CVKM_VEC3_MISC_OPERATIONS_FOR_INTS(bvec3, int8_t)
CVKM_VEC3_MISC_OPERATIONS_FOR_UNSIGNED_INTS(ubvec3, uint8_t)
CVKM_VEC3_MISC_OPERATIONS_FOR_INTS(svec3, int16_t)
CVKM_VEC3_MISC_OPERATIONS_FOR_UNSIGNED_INTS(usvec3, uint16_t)
CVKM_VEC3_MISC_OPERATIONS_FOR_INTS(ivec3, int32_t)
CVKM_VEC3_MISC_OPERATIONS_FOR_UNSIGNED_INTS(uvec3, uint32_t)
CVKM_VEC3_MISC_OPERATIONS_FOR_INTS(lvec3, int64_t)
CVKM_VEC3_MISC_OPERATIONS_FOR_UNSIGNED_INTS(ulvec3, uint64_t)
CVKM_VEC3_MISC_OPERATIONS(vec3, float)
CVKM_VEC3_MISC_OPERATIONS(dvec3, double)

#define CVKM_VEC4_MISC_OPERATIONS_FOR_UNSIGNED_INTS(vec_type, scalar_type) static scalar_type vkm_##vec_type##_dot(\
  const vkm_##vec_type* a,\
  const vkm_##vec_type* b\
) {\
  return a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w;\
}\
\
static scalar_type vkm_##vec_type##_sqr_magnitude(const vkm_##vec_type* vec) {\
  return vec->x * vec->x + vec->y * vec->y + vec->z * vec->z + vec->w * vec->w;\
}\
\
static scalar_type vkm_##vec_type##_magnitude(const vkm_##vec_type* vec) {\
  return vkm_sqrt(vkm_##vec_type##_sqr_magnitude(vec));\
}\
\
static void vkm_##vec_type##_clear(vkm_##vec_type* vec) {\
  *vec = (vkm_##vec_type){ { (scalar_type)0, (scalar_type)0, (scalar_type)0, (scalar_type)0 } };\
}

#define CVKM_VEC4_INVERT(vec_type) static void vkm_##vec_type##_invert(\
  const vkm_##vec_type* vec,\
  vkm_##vec_type* result\
) {\
  *result = (vkm_##vec_type){ { -vec->x, -vec->y, -vec->z, -vec->w } };\
}

#define CVKM_VEC4_NORMALIZE(vec_type, scalar_type) static void vkm_##vec_type##_normalize(\
  const vkm_##vec_type* vec,\
  vkm_##vec_type* result\
) {\
  const scalar_type magnitude = vkm_##vec_type##_magnitude(vec);\
  if (magnitude > (scalar_type)0) {\
    vkm_##vec_type##_div_scalar(vec, magnitude, result);\
  }\
}

#define CVKM_VEC4_MISC_OPERATIONS_FOR_INTS(vec_type, scalar_type)\
  CVKM_VEC4_MISC_OPERATIONS_FOR_UNSIGNED_INTS(vec_type, scalar_type)\
  CVKM_VEC4_INVERT(vec_type)

#define CVKM_VEC4_MISC_OPERATIONS(vec_type, scalar_type)\
  CVKM_VEC4_MISC_OPERATIONS_FOR_INTS(vec_type, scalar_type)\
  CVKM_VEC4_NORMALIZE(vec_type, scalar_type)

CVKM_VEC4_MISC_OPERATIONS_FOR_INTS(bvec4, int8_t)
CVKM_VEC4_MISC_OPERATIONS_FOR_UNSIGNED_INTS(ubvec4, uint8_t)
CVKM_VEC4_MISC_OPERATIONS_FOR_INTS(svec4, int16_t)
CVKM_VEC4_MISC_OPERATIONS_FOR_UNSIGNED_INTS(usvec4, uint16_t)
CVKM_VEC4_MISC_OPERATIONS_FOR_INTS(ivec4, int32_t)
CVKM_VEC4_MISC_OPERATIONS_FOR_UNSIGNED_INTS(uvec4, uint32_t)
CVKM_VEC4_MISC_OPERATIONS_FOR_INTS(lvec4, int64_t)
CVKM_VEC4_MISC_OPERATIONS_FOR_UNSIGNED_INTS(ulvec4, uint64_t)
CVKM_VEC4_MISC_OPERATIONS(vec4, float)
CVKM_VEC4_MISC_OPERATIONS(dvec4, double)

static float vkm_quat_sqr_magnitude(const vkm_quat* quaternion) {
  return vkm_vec4_sqr_magnitude((const vkm_vec4*)quaternion);
}

static float vkm_quat_magnitude(const vkm_quat* quaternion) {
  return vkm_vec4_magnitude((const vkm_vec4*)quaternion);
}

static void vkm_mat4_invert(const vkm_mat4* mat, vkm_mat4* result) {
  const float
    sub1  = mat->m22 * mat->m33 - mat->m23 * mat->m32,  sub2  = mat->m02 * mat->m13 - mat->m03 * mat->m12,  sub3  = mat->m20 * mat->m33 - mat->m23 * mat->m30,
    sub4  = mat->m00 * mat->m13 - mat->m03 * mat->m10,  sub5  = mat->m21 * mat->m33 - mat->m23 * mat->m31,  sub6  = mat->m01 * mat->m13 - mat->m03 * mat->m11,
    sub7  = mat->m20 * mat->m31 - mat->m21 * mat->m30,  sub8  = mat->m00 * mat->m11 - mat->m01 * mat->m10,  sub9  = mat->m21 * mat->m32 - mat->m22 * mat->m31,
    sub10 = mat->m01 * mat->m12 - mat->m02 * mat->m11,  sub11 = mat->m20 * mat->m32 - mat->m22 * mat->m30,  sub12 = mat->m00 * mat->m12 - mat->m02 * mat->m10,

    inverse_determinant = 1.0f / (sub8 * sub1 + sub4 * sub9 + sub10 * sub3 + sub2 * sub7 - sub12 * sub5 - sub6 * sub11),
    negative_determinant = -inverse_determinant;

  result->m00 = (mat->m11 * sub1  - mat->m12 * sub5  + mat->m13 * sub9)  * inverse_determinant;
  result->m01 = (mat->m01 * sub1  - mat->m02 * sub5  + mat->m03 * sub9)  * negative_determinant;
  result->m02 = (mat->m31 * sub2  - mat->m32 * sub6  + mat->m33 * sub10) * inverse_determinant;
  result->m03 = (mat->m21 * sub2  - mat->m22 * sub6  + mat->m23 * sub10) * negative_determinant;

  result->m10 = (mat->m10 * sub1  - mat->m12 * sub3  + mat->m13 * sub11) * negative_determinant;
  result->m11 = (mat->m00 * sub1  - mat->m02 * sub3  + mat->m03 * sub11) * inverse_determinant;
  result->m12 = (mat->m30 * sub2  - mat->m32 * sub4  + mat->m33 * sub12) * negative_determinant;
  result->m13 = (mat->m20 * sub2  - mat->m22 * sub4  + mat->m23 * sub12) * inverse_determinant;

  result->m20 = (mat->m10 * sub5  - mat->m11 * sub3  + mat->m13 * sub7)  * inverse_determinant;
  result->m21 = (mat->m00 * sub5  - mat->m01 * sub3  + mat->m03 * sub7)  * negative_determinant;
  result->m22 = (mat->m30 * sub6  - mat->m31 * sub4  + mat->m33 * sub8)  * inverse_determinant;
  result->m23 = (mat->m20 * sub6  - mat->m21 * sub4  + mat->m23 * sub8)  * negative_determinant;

  result->m30 = (mat->m10 * sub9  - mat->m11 * sub11 + mat->m12 * sub7)  * negative_determinant;
  result->m31 = (mat->m00 * sub9  - mat->m01 * sub11 + mat->m02 * sub7)  * inverse_determinant;
  result->m32 = (mat->m30 * sub10 - mat->m31 * sub12 + mat->m32 * sub8)  * negative_determinant;
  result->m33 = (mat->m20 * sub10 - mat->m21 * sub12 + mat->m22 * sub8)  * inverse_determinant;
}

static void vkm_mat3_transpose(const vkm_mat3* mat, vkm_mat3* result) {
  *result = (vkm_mat3){
    .m00 = mat->m00, .m01 = mat->m10, .m02 = mat->m20,
    .m10 = mat->m01, .m11 = mat->m11, .m12 = mat->m21,
    .m20 = mat->m02, .m21 = mat->m12, .m22 = mat->m22,
  };
}

static void vkm_mat4_transpose(const vkm_mat4* mat, vkm_mat4* result) {
  *result = (vkm_mat4){
    .m00 = mat->m00, .m01 = mat->m10, .m02 = mat->m20, .m03 = mat->m30,
    .m10 = mat->m01, .m11 = mat->m11, .m12 = mat->m21, .m13 = mat->m31,
    .m20 = mat->m02, .m21 = mat->m12, .m22 = mat->m22, .m23 = mat->m32,
    .m30 = mat->m03, .m31 = mat->m13, .m32 = mat->m23, .m33 = mat->m33,
  };
}

#define vkm_dot(a, ...) _Generic(a,\
  vkm_bvec2*: vkm_bvec2_dot,\
  vkm_ubvec2*: vkm_ubvec2_dot,\
  vkm_svec2*: vkm_svec2_dot,\
  vkm_usvec2*: vkm_usvec2_dot,\
  vkm_ivec2*: vkm_ivec2_dot,\
  vkm_uvec2*: vkm_uvec2_dot,\
  vkm_lvec2*: vkm_lvec2_dot,\
  vkm_ulvec2*: vkm_ulvec2_dot,\
  vkm_vec2*: vkm_vec2_dot,\
  vkm_dvec2*: vkm_dvec2_dot,\
  vkm_bvec3*: vkm_bvec3_dot,\
  vkm_ubvec3*: vkm_ubvec3_dot,\
  vkm_svec3*: vkm_svec3_dot,\
  vkm_usvec3*: vkm_usvec3_dot,\
  vkm_ivec3*: vkm_ivec3_dot,\
  vkm_uvec3*: vkm_uvec3_dot,\
  vkm_lvec3*: vkm_lvec3_dot,\
  vkm_ulvec3*: vkm_ulvec3_dot,\
  vkm_vec3*: vkm_vec3_dot,\
  vkm_dvec3*: vkm_dvec3_dot,\
  vkm_bvec4*: vkm_bvec4_dot,\
  vkm_ubvec4*: vkm_ubvec4_dot,\
  vkm_svec4*: vkm_svec4_dot,\
  vkm_usvec4*: vkm_usvec4_dot,\
  vkm_ivec4*: vkm_ivec4_dot,\
  vkm_uvec4*: vkm_uvec4_dot,\
  vkm_lvec4*: vkm_lvec4_dot,\
  vkm_ulvec4*: vkm_ulvec4_dot,\
  vkm_vec4*: vkm_vec4_dot,\
  vkm_dvec4*: vkm_dvec4_dot,\
  const vkm_bvec2*: vkm_bvec2_dot,\
  const vkm_ubvec2*: vkm_ubvec2_dot,\
  const vkm_svec2*: vkm_svec2_dot,\
  const vkm_usvec2*: vkm_usvec2_dot,\
  const vkm_ivec2*: vkm_ivec2_dot,\
  const vkm_uvec2*: vkm_uvec2_dot,\
  const vkm_lvec2*: vkm_lvec2_dot,\
  const vkm_ulvec2*: vkm_ulvec2_dot,\
  const vkm_vec2*: vkm_vec2_dot,\
  const vkm_dvec2*: vkm_dvec2_dot,\
  const vkm_bvec3*: vkm_bvec3_dot,\
  const vkm_ubvec3*: vkm_ubvec3_dot,\
  const vkm_svec3*: vkm_svec3_dot,\
  const vkm_usvec3*: vkm_usvec3_dot,\
  const vkm_ivec3*: vkm_ivec3_dot,\
  const vkm_uvec3*: vkm_uvec3_dot,\
  const vkm_lvec3*: vkm_lvec3_dot,\
  const vkm_ulvec3*: vkm_ulvec3_dot,\
  const vkm_vec3*: vkm_vec3_dot,\
  const vkm_dvec3*: vkm_dvec3_dot,\
  const vkm_bvec4*: vkm_bvec4_dot,\
  const vkm_ubvec4*: vkm_ubvec4_dot,\
  const vkm_svec4*: vkm_svec4_dot,\
  const vkm_usvec4*: vkm_usvec4_dot,\
  const vkm_ivec4*: vkm_ivec4_dot,\
  const vkm_uvec4*: vkm_uvec4_dot,\
  const vkm_lvec4*: vkm_lvec4_dot,\
  const vkm_ulvec4*: vkm_ulvec4_dot,\
  const vkm_vec4*: vkm_vec4_dot,\
  const vkm_dvec4*: vkm_dvec4_dot\
)(a, __VA_ARGS__)

#define vkm_cross(a, b, result) _Generic(result,\
  vkm_bvec3*: vkm_bvec3_cross,\
  vkm_ubvec3*: vkm_ubvec3_cross,\
  vkm_svec3*: vkm_svec3_cross,\
  vkm_usvec3*: vkm_usvec3_cross,\
  vkm_ivec3*: vkm_ivec3_cross,\
  vkm_uvec3*: vkm_uvec3_cross,\
  vkm_lvec3*: vkm_lvec3_cross,\
  vkm_ulvec3*: vkm_ulvec3_cross,\
  vkm_vec3*: vkm_vec3_cross,\
  vkm_dvec3*: vkm_dvec3_cross\
)(a, b, result)

#define vkm_sqr_magnitude(...) _Generic(__VA_ARGS__,\
  vkm_bvec2*: vkm_bvec2_sqr_magnitude,\
  vkm_ubvec2*: vkm_ubvec2_sqr_magnitude,\
  vkm_svec2*: vkm_svec2_sqr_magnitude,\
  vkm_usvec2*: vkm_usvec2_sqr_magnitude,\
  vkm_ivec2*: vkm_ivec2_sqr_magnitude,\
  vkm_uvec2*: vkm_uvec2_sqr_magnitude,\
  vkm_lvec2*: vkm_lvec2_sqr_magnitude,\
  vkm_ulvec2*: vkm_ulvec2_sqr_magnitude,\
  vkm_vec2*: vkm_vec2_sqr_magnitude,\
  vkm_dvec2*: vkm_dvec2_sqr_magnitude,\
  vkm_bvec3*: vkm_bvec3_sqr_magnitude,\
  vkm_ubvec3*: vkm_ubvec3_sqr_magnitude,\
  vkm_svec3*: vkm_svec3_sqr_magnitude,\
  vkm_usvec3*: vkm_usvec3_sqr_magnitude,\
  vkm_ivec3*: vkm_ivec3_sqr_magnitude,\
  vkm_uvec3*: vkm_uvec3_sqr_magnitude,\
  vkm_lvec3*: vkm_lvec3_sqr_magnitude,\
  vkm_ulvec3*: vkm_ulvec3_sqr_magnitude,\
  vkm_vec3*: vkm_vec3_sqr_magnitude,\
  vkm_dvec3*: vkm_dvec3_sqr_magnitude,\
  vkm_bvec4*: vkm_bvec4_sqr_magnitude,\
  vkm_ubvec4*: vkm_ubvec4_sqr_magnitude,\
  vkm_svec4*: vkm_svec4_sqr_magnitude,\
  vkm_usvec4*: vkm_usvec4_sqr_magnitude,\
  vkm_ivec4*: vkm_ivec4_sqr_magnitude,\
  vkm_uvec4*: vkm_uvec4_sqr_magnitude,\
  vkm_lvec4*: vkm_lvec4_sqr_magnitude,\
  vkm_ulvec4*: vkm_ulvec4_sqr_magnitude,\
  vkm_vec4*: vkm_vec4_sqr_magnitude,\
  vkm_dvec4*: vkm_dvec4_sqr_magnitude,\
  vkm_quat*: vkm_quat_sqr_magnitude,\
  const vkm_bvec2*: vkm_bvec2_sqr_magnitude,\
  const vkm_ubvec2*: vkm_ubvec2_sqr_magnitude,\
  const vkm_svec2*: vkm_svec2_sqr_magnitude,\
  const vkm_usvec2*: vkm_usvec2_sqr_magnitude,\
  const vkm_ivec2*: vkm_ivec2_sqr_magnitude,\
  const vkm_uvec2*: vkm_uvec2_sqr_magnitude,\
  const vkm_lvec2*: vkm_lvec2_sqr_magnitude,\
  const vkm_ulvec2*: vkm_ulvec2_sqr_magnitude,\
  const vkm_vec2*: vkm_vec2_sqr_magnitude,\
  const vkm_dvec2*: vkm_dvec2_sqr_magnitude,\
  const vkm_bvec3*: vkm_bvec3_sqr_magnitude,\
  const vkm_ubvec3*: vkm_ubvec3_sqr_magnitude,\
  const vkm_svec3*: vkm_svec3_sqr_magnitude,\
  const vkm_usvec3*: vkm_usvec3_sqr_magnitude,\
  const vkm_ivec3*: vkm_ivec3_sqr_magnitude,\
  const vkm_uvec3*: vkm_uvec3_sqr_magnitude,\
  const vkm_lvec3*: vkm_lvec3_sqr_magnitude,\
  const vkm_ulvec3*: vkm_ulvec3_sqr_magnitude,\
  const vkm_vec3*: vkm_vec3_sqr_magnitude,\
  const vkm_dvec3*: vkm_dvec3_sqr_magnitude,\
  const vkm_bvec4*: vkm_bvec4_sqr_magnitude,\
  const vkm_ubvec4*: vkm_ubvec4_sqr_magnitude,\
  const vkm_svec4*: vkm_svec4_sqr_magnitude,\
  const vkm_usvec4*: vkm_usvec4_sqr_magnitude,\
  const vkm_ivec4*: vkm_ivec4_sqr_magnitude,\
  const vkm_uvec4*: vkm_uvec4_sqr_magnitude,\
  const vkm_lvec4*: vkm_lvec4_sqr_magnitude,\
  const vkm_ulvec4*: vkm_ulvec4_sqr_magnitude,\
  const vkm_vec4*: vkm_vec4_sqr_magnitude,\
  const vkm_dvec4*: vkm_dvec4_sqr_magnitude,\
  const vkm_quat*: vkm_quat_sqr_magnitude\
)(__VA_ARGS__)

#define vkm_magnitude(...) _Generic(__VA_ARGS__,\
  vkm_bvec2*: vkm_bvec2_magnitude,\
  vkm_ubvec2*: vkm_ubvec2_magnitude,\
  vkm_svec2*: vkm_svec2_magnitude,\
  vkm_usvec2*: vkm_usvec2_magnitude,\
  vkm_ivec2*: vkm_ivec2_magnitude,\
  vkm_uvec2*: vkm_uvec2_magnitude,\
  vkm_lvec2*: vkm_lvec2_magnitude,\
  vkm_ulvec2*: vkm_ulvec2_magnitude,\
  vkm_vec2*: vkm_vec2_magnitude,\
  vkm_dvec2*: vkm_dvec2_magnitude,\
  vkm_bvec3*: vkm_bvec3_magnitude,\
  vkm_ubvec3*: vkm_ubvec3_magnitude,\
  vkm_svec3*: vkm_svec3_magnitude,\
  vkm_usvec3*: vkm_usvec3_magnitude,\
  vkm_ivec3*: vkm_ivec3_magnitude,\
  vkm_uvec3*: vkm_uvec3_magnitude,\
  vkm_lvec3*: vkm_lvec3_magnitude,\
  vkm_ulvec3*: vkm_ulvec3_magnitude,\
  vkm_vec3*: vkm_vec3_magnitude,\
  vkm_dvec3*: vkm_dvec3_magnitude,\
  vkm_bvec4*: vkm_bvec4_magnitude,\
  vkm_ubvec4*: vkm_ubvec4_magnitude,\
  vkm_svec4*: vkm_svec4_magnitude,\
  vkm_usvec4*: vkm_usvec4_magnitude,\
  vkm_ivec4*: vkm_ivec4_magnitude,\
  vkm_uvec4*: vkm_uvec4_magnitude,\
  vkm_lvec4*: vkm_lvec4_magnitude,\
  vkm_ulvec4*: vkm_ulvec4_magnitude,\
  vkm_vec4*: vkm_vec4_magnitude,\
  vkm_dvec4*: vkm_dvec4_magnitude,\
  vkm_quat*: vkm_quat_magnitude,\
  const vkm_bvec2*: vkm_bvec2_magnitude,\
  const vkm_ubvec2*: vkm_ubvec2_magnitude,\
  const vkm_svec2*: vkm_svec2_magnitude,\
  const vkm_usvec2*: vkm_usvec2_magnitude,\
  const vkm_ivec2*: vkm_ivec2_magnitude,\
  const vkm_uvec2*: vkm_uvec2_magnitude,\
  const vkm_lvec2*: vkm_lvec2_magnitude,\
  const vkm_ulvec2*: vkm_ulvec2_magnitude,\
  const vkm_vec2*: vkm_vec2_magnitude,\
  const vkm_dvec2*: vkm_dvec2_magnitude,\
  const vkm_bvec3*: vkm_bvec3_magnitude,\
  const vkm_ubvec3*: vkm_ubvec3_magnitude,\
  const vkm_svec3*: vkm_svec3_magnitude,\
  const vkm_usvec3*: vkm_usvec3_magnitude,\
  const vkm_ivec3*: vkm_ivec3_magnitude,\
  const vkm_uvec3*: vkm_uvec3_magnitude,\
  const vkm_lvec3*: vkm_lvec3_magnitude,\
  const vkm_ulvec3*: vkm_ulvec3_magnitude,\
  const vkm_vec3*: vkm_vec3_magnitude,\
  const vkm_dvec3*: vkm_dvec3_magnitude,\
  const vkm_bvec4*: vkm_bvec4_magnitude,\
  const vkm_ubvec4*: vkm_ubvec4_magnitude,\
  const vkm_svec4*: vkm_svec4_magnitude,\
  const vkm_usvec4*: vkm_usvec4_magnitude,\
  const vkm_ivec4*: vkm_ivec4_magnitude,\
  const vkm_uvec4*: vkm_uvec4_magnitude,\
  const vkm_lvec4*: vkm_lvec4_magnitude,\
  const vkm_ulvec4*: vkm_ulvec4_magnitude,\
  const vkm_vec4*: vkm_vec4_magnitude,\
  const vkm_dvec4*: vkm_dvec4_magnitude,\
  const vkm_quat*: vkm_quat_magnitude\
)(__VA_ARGS__)

#define vkm_length(...) vkm_magnitude(__VA_ARGS__)

#define vkm_normalize(vec, result) _Generic(result,\
  vkm_vec2*: vkm_vec2_normalize,\
  vkm_dvec2*: vkm_dvec2_normalize,\
  vkm_vec3*: vkm_vec3_normalize,\
  vkm_dvec3*: vkm_dvec3_normalize,\
  vkm_vec4*: vkm_vec4_normalize,\
  vkm_dvec4*: vkm_dvec4_normalize\
)(vec, result)

#define vkm_clear(vec) _Generic(vec,\
  vkm_bvec2*: vkm_bvec2_clear,\
  vkm_ubvec2*: vkm_ubvec2_clear,\
  vkm_svec2*: vkm_svec2_clear,\
  vkm_usvec2*: vkm_usvec2_clear,\
  vkm_ivec2*: vkm_ivec2_clear,\
  vkm_uvec2*: vkm_uvec2_clear,\
  vkm_lvec2*: vkm_lvec2_clear,\
  vkm_ulvec2*: vkm_ulvec2_clear,\
  vkm_vec2*: vkm_vec2_clear,\
  vkm_dvec2*: vkm_dvec2_clear,\
  vkm_bvec3*: vkm_bvec3_clear,\
  vkm_ubvec3*: vkm_ubvec3_clear,\
  vkm_svec3*: vkm_svec3_clear,\
  vkm_usvec3*: vkm_usvec3_clear,\
  vkm_ivec3*: vkm_ivec3_clear,\
  vkm_uvec3*: vkm_uvec3_clear,\
  vkm_lvec3*: vkm_lvec3_clear,\
  vkm_ulvec3*: vkm_ulvec3_clear,\
  vkm_vec3*: vkm_vec3_clear,\
  vkm_dvec3*: vkm_dvec3_clear,\
  vkm_bvec4*: vkm_bvec4_clear,\
  vkm_ubvec4*: vkm_ubvec4_clear,\
  vkm_svec4*: vkm_svec4_clear,\
  vkm_usvec4*: vkm_usvec4_clear,\
  vkm_ivec4*: vkm_ivec4_clear,\
  vkm_uvec4*: vkm_uvec4_clear,\
  vkm_lvec4*: vkm_lvec4_clear,\
  vkm_ulvec4*: vkm_ulvec4_clear,\
  vkm_vec4*: vkm_vec4_clear,\
  vkm_dvec4*: vkm_dvec4_clear\
)(vec)

#define vkm_invert(a, result) _Generic(result,\
  vkm_bvec2*: vkm_bvec2_invert,\
  vkm_svec2*: vkm_svec2_invert,\
  vkm_ivec2*: vkm_ivec2_invert,\
  vkm_lvec2*: vkm_lvec2_invert,\
  vkm_vec2*: vkm_vec2_invert,\
  vkm_dvec2*: vkm_dvec2_invert,\
  vkm_bvec3*: vkm_bvec3_invert,\
  vkm_svec3*: vkm_svec3_invert,\
  vkm_ivec3*: vkm_ivec3_invert,\
  vkm_lvec3*: vkm_lvec3_invert,\
  vkm_vec3*: vkm_vec3_invert,\
  vkm_dvec3*: vkm_dvec3_invert,\
  vkm_bvec4*: vkm_bvec4_invert,\
  vkm_svec4*: vkm_svec4_invert,\
  vkm_ivec4*: vkm_ivec4_invert,\
  vkm_lvec4*: vkm_lvec4_invert,\
  vkm_vec4*: vkm_vec4_invert,\
  vkm_dvec4*: vkm_dvec4_invert,\
  vkm_mat4*: vkm_mat4_invert\
)(a, result)

#define vkm_transpose(mat, result) _Generic(result,\
  vkm_mat3*: vkm_mat3_transpose,\
  vkm_mat4*: vkm_mat4_transpose\
)(mat, result)

#define CVKM_VEC2_LOGICAL_OPERATION(type, operation, operator) static bool vkm_##type##_##operation(\
const vkm_##type* a,\
const vkm_##type* b\
) {\
  return a->x operator b->x && a->y operator b->y;\
}

#define CVKM_VEC2_LOGICAL_OPERATIONS(type) CVKM_VEC2_LOGICAL_OPERATION(type, eq, ==)\
CVKM_VEC2_LOGICAL_OPERATION(type, lt, <)\
CVKM_VEC2_LOGICAL_OPERATION(type, gt, >)\
CVKM_VEC2_LOGICAL_OPERATION(type, le, <=)\
CVKM_VEC2_LOGICAL_OPERATION(type, ge, >=)

CVKM_VEC2_LOGICAL_OPERATIONS(bvec2)
CVKM_VEC2_LOGICAL_OPERATIONS(ubvec2)
CVKM_VEC2_LOGICAL_OPERATIONS(svec2)
CVKM_VEC2_LOGICAL_OPERATIONS(usvec2)
CVKM_VEC2_LOGICAL_OPERATIONS(ivec2)
CVKM_VEC2_LOGICAL_OPERATIONS(uvec2)
CVKM_VEC2_LOGICAL_OPERATIONS(lvec2)
CVKM_VEC2_LOGICAL_OPERATIONS(ulvec2)
CVKM_VEC2_LOGICAL_OPERATIONS(vec2)
CVKM_VEC2_LOGICAL_OPERATIONS(dvec2)

#define CVKM_VEC3_LOGICAL_OPERATION(type, operation, operator) static bool vkm_##type##_##operation(\
  const vkm_##type* a,\
  const vkm_##type* b\
) {\
  return a->x operator b->x && a->y operator b->y && a->z operator b->z;\
}

#define CVKM_VEC3_LOGICAL_OPERATIONS(type) CVKM_VEC3_LOGICAL_OPERATION(type, eq, ==)\
  CVKM_VEC3_LOGICAL_OPERATION(type, lt, <)\
  CVKM_VEC3_LOGICAL_OPERATION(type, gt, >)\
  CVKM_VEC3_LOGICAL_OPERATION(type, le, <=)\
  CVKM_VEC3_LOGICAL_OPERATION(type, ge, >=)

CVKM_VEC3_LOGICAL_OPERATIONS(bvec3)
CVKM_VEC3_LOGICAL_OPERATIONS(ubvec3)
CVKM_VEC3_LOGICAL_OPERATIONS(svec3)
CVKM_VEC3_LOGICAL_OPERATIONS(usvec3)
CVKM_VEC3_LOGICAL_OPERATIONS(ivec3)
CVKM_VEC3_LOGICAL_OPERATIONS(uvec3)
CVKM_VEC3_LOGICAL_OPERATIONS(lvec3)
CVKM_VEC3_LOGICAL_OPERATIONS(ulvec3)
CVKM_VEC3_LOGICAL_OPERATIONS(vec3)
CVKM_VEC3_LOGICAL_OPERATIONS(dvec3)

#define CVKM_VEC4_LOGICAL_OPERATION(type, operation, operator) static bool vkm_##type##_##operation(\
  const vkm_##type* a,\
  const vkm_##type* b\
) {\
  return a->x operator b->x && a->y operator b->y && a->z operator b->z && a->w operator b->w;\
}

#define CVKM_VEC4_LOGICAL_OPERATIONS(type) CVKM_VEC4_LOGICAL_OPERATION(type, eq, ==)\
CVKM_VEC4_LOGICAL_OPERATION(type, lt, <)\
CVKM_VEC4_LOGICAL_OPERATION(type, gt, >)\
CVKM_VEC4_LOGICAL_OPERATION(type, le, <=)\
CVKM_VEC4_LOGICAL_OPERATION(type, ge, >=)

CVKM_VEC4_LOGICAL_OPERATIONS(bvec4)
CVKM_VEC4_LOGICAL_OPERATIONS(ubvec4)
CVKM_VEC4_LOGICAL_OPERATIONS(svec4)
CVKM_VEC4_LOGICAL_OPERATIONS(usvec4)
CVKM_VEC4_LOGICAL_OPERATIONS(ivec4)
CVKM_VEC4_LOGICAL_OPERATIONS(uvec4)
CVKM_VEC4_LOGICAL_OPERATIONS(lvec4)
CVKM_VEC4_LOGICAL_OPERATIONS(ulvec4)
CVKM_VEC4_LOGICAL_OPERATIONS(vec4)
CVKM_VEC4_LOGICAL_OPERATIONS(dvec4)

#define vkm_eq(a, ...) _Generic(a,\
  vkm_bvec2*: vkm_bvec2_eq,\
  vkm_ubvec2*: vkm_ubvec2_eq,\
  vkm_svec2*: vkm_svec2_eq,\
  vkm_usvec2*: vkm_usvec2_eq,\
  vkm_ivec2*: vkm_ivec2_eq,\
  vkm_uvec2*: vkm_uvec2_eq,\
  vkm_lvec2*: vkm_lvec2_eq,\
  vkm_ulvec2*: vkm_ulvec2_eq,\
  vkm_vec2*: vkm_vec2_eq,\
  vkm_dvec2*: vkm_dvec2_eq,\
  vkm_bvec3*: vkm_bvec3_eq,\
  vkm_ubvec3*: vkm_ubvec3_eq,\
  vkm_svec3*: vkm_svec3_eq,\
  vkm_usvec3*: vkm_usvec3_eq,\
  vkm_ivec3*: vkm_ivec3_eq,\
  vkm_uvec3*: vkm_uvec3_eq,\
  vkm_lvec3*: vkm_lvec3_eq,\
  vkm_ulvec3*: vkm_ulvec3_eq,\
  vkm_vec3*: vkm_vec3_eq,\
  vkm_dvec3*: vkm_dvec3_eq,\
  vkm_bvec4*: vkm_bvec4_eq,\
  vkm_ubvec4*: vkm_ubvec4_eq,\
  vkm_svec4*: vkm_svec4_eq,\
  vkm_usvec4*: vkm_usvec4_eq,\
  vkm_ivec4*: vkm_ivec4_eq,\
  vkm_uvec4*: vkm_uvec4_eq,\
  vkm_lvec4*: vkm_lvec4_eq,\
  vkm_ulvec4*: vkm_ulvec4_eq,\
  vkm_vec4*: vkm_vec4_eq,\
  vkm_dvec4*: vkm_dvec4_eq,\
  const vkm_bvec2*: vkm_bvec2_eq,\
  const vkm_ubvec2*: vkm_ubvec2_eq,\
  const vkm_svec2*: vkm_svec2_eq,\
  const vkm_usvec2*: vkm_usvec2_eq,\
  const vkm_ivec2*: vkm_ivec2_eq,\
  const vkm_uvec2*: vkm_uvec2_eq,\
  const vkm_lvec2*: vkm_lvec2_eq,\
  const vkm_ulvec2*: vkm_ulvec2_eq,\
  const vkm_vec2*: vkm_vec2_eq,\
  const vkm_dvec2*: vkm_dvec2_eq,\
  const vkm_bvec3*: vkm_bvec3_eq,\
  const vkm_ubvec3*: vkm_ubvec3_eq,\
  const vkm_svec3*: vkm_svec3_eq,\
  const vkm_usvec3*: vkm_usvec3_eq,\
  const vkm_ivec3*: vkm_ivec3_eq,\
  const vkm_uvec3*: vkm_uvec3_eq,\
  const vkm_lvec3*: vkm_lvec3_eq,\
  const vkm_ulvec3*: vkm_ulvec3_eq,\
  const vkm_vec3*: vkm_vec3_eq,\
  const vkm_dvec3*: vkm_dvec3_eq,\
  const vkm_bvec4*: vkm_bvec4_eq,\
  const vkm_ubvec4*: vkm_ubvec4_eq,\
  const vkm_svec4*: vkm_svec4_eq,\
  const vkm_usvec4*: vkm_usvec4_eq,\
  const vkm_ivec4*: vkm_ivec4_eq,\
  const vkm_uvec4*: vkm_uvec4_eq,\
  const vkm_lvec4*: vkm_lvec4_eq,\
  const vkm_ulvec4*: vkm_ulvec4_eq,\
  const vkm_vec4*: vkm_vec4_eq,\
  const vkm_dvec4*: vkm_dvec4_eq\
)(a, __VA_ARGS__)

#define vkm_lt(a, ...) _Generic(a,\
  vkm_bvec2*: vkm_bvec2_lt,\
  vkm_ubvec2*: vkm_ubvec2_lt,\
  vkm_svec2*: vkm_svec2_lt,\
  vkm_usvec2*: vkm_usvec2_lt,\
  vkm_ivec2*: vkm_ivec2_lt,\
  vkm_uvec2*: vkm_uvec2_lt,\
  vkm_lvec2*: vkm_lvec2_lt,\
  vkm_ulvec2*: vkm_ulvec2_lt,\
  vkm_vec2*: vkm_vec2_lt,\
  vkm_dvec2*: vkm_dvec2_lt,\
  vkm_bvec3*: vkm_bvec3_lt,\
  vkm_ubvec3*: vkm_ubvec3_lt,\
  vkm_svec3*: vkm_svec3_lt,\
  vkm_usvec3*: vkm_usvec3_lt,\
  vkm_ivec3*: vkm_ivec3_lt,\
  vkm_uvec3*: vkm_uvec3_lt,\
  vkm_lvec3*: vkm_lvec3_lt,\
  vkm_ulvec3*: vkm_ulvec3_lt,\
  vkm_vec3*: vkm_vec3_lt,\
  vkm_dvec3*: vkm_dvec3_lt,\
  vkm_bvec4*: vkm_bvec4_lt,\
  vkm_ubvec4*: vkm_ubvec4_lt,\
  vkm_svec4*: vkm_svec4_lt,\
  vkm_usvec4*: vkm_usvec4_lt,\
  vkm_ivec4*: vkm_ivec4_lt,\
  vkm_uvec4*: vkm_uvec4_lt,\
  vkm_lvec4*: vkm_lvec4_lt,\
  vkm_ulvec4*: vkm_ulvec4_lt,\
  vkm_vec4*: vkm_vec4_lt,\
  vkm_dvec4*: vkm_dvec4_lt,\
  const vkm_bvec2*: vkm_bvec2_lt,\
  const vkm_ubvec2*: vkm_ubvec2_lt,\
  const vkm_svec2*: vkm_svec2_lt,\
  const vkm_usvec2*: vkm_usvec2_lt,\
  const vkm_ivec2*: vkm_ivec2_lt,\
  const vkm_uvec2*: vkm_uvec2_lt,\
  const vkm_lvec2*: vkm_lvec2_lt,\
  const vkm_ulvec2*: vkm_ulvec2_lt,\
  const vkm_vec2*: vkm_vec2_lt,\
  const vkm_dvec2*: vkm_dvec2_lt,\
  const vkm_bvec3*: vkm_bvec3_lt,\
  const vkm_ubvec3*: vkm_ubvec3_lt,\
  const vkm_svec3*: vkm_svec3_lt,\
  const vkm_usvec3*: vkm_usvec3_lt,\
  const vkm_ivec3*: vkm_ivec3_lt,\
  const vkm_uvec3*: vkm_uvec3_lt,\
  const vkm_lvec3*: vkm_lvec3_lt,\
  const vkm_ulvec3*: vkm_ulvec3_lt,\
  const vkm_vec3*: vkm_vec3_lt,\
  const vkm_dvec3*: vkm_dvec3_lt,\
  const vkm_bvec4*: vkm_bvec4_lt,\
  const vkm_ubvec4*: vkm_ubvec4_lt,\
  const vkm_svec4*: vkm_svec4_lt,\
  const vkm_usvec4*: vkm_usvec4_lt,\
  const vkm_ivec4*: vkm_ivec4_lt,\
  const vkm_uvec4*: vkm_uvec4_lt,\
  const vkm_lvec4*: vkm_lvec4_lt,\
  const vkm_ulvec4*: vkm_ulvec4_lt,\
  const vkm_vec4*: vkm_vec4_lt,\
  const vkm_dvec4*: vkm_dvec4_lt\
)(a, __VA_ARGS__)

#define vkm_gt(a, ...) _Generic(a,\
  vkm_bvec2*: vkm_bvec2_gt,\
  vkm_ubvec2*: vkm_ubvec2_gt,\
  vkm_svec2*: vkm_svec2_gt,\
  vkm_usvec2*: vkm_usvec2_gt,\
  vkm_ivec2*: vkm_ivec2_gt,\
  vkm_uvec2*: vkm_uvec2_gt,\
  vkm_lvec2*: vkm_lvec2_gt,\
  vkm_ulvec2*: vkm_ulvec2_gt,\
  vkm_vec2*: vkm_vec2_gt,\
  vkm_dvec2*: vkm_dvec2_gt,\
  vkm_bvec3*: vkm_bvec3_gt,\
  vkm_ubvec3*: vkm_ubvec3_gt,\
  vkm_svec3*: vkm_svec3_gt,\
  vkm_usvec3*: vkm_usvec3_gt,\
  vkm_ivec3*: vkm_ivec3_gt,\
  vkm_uvec3*: vkm_uvec3_gt,\
  vkm_lvec3*: vkm_lvec3_gt,\
  vkm_ulvec3*: vkm_ulvec3_gt,\
  vkm_vec3*: vkm_vec3_gt,\
  vkm_dvec3*: vkm_dvec3_gt,\
  vkm_bvec4*: vkm_bvec4_gt,\
  vkm_ubvec4*: vkm_ubvec4_gt,\
  vkm_svec4*: vkm_svec4_gt,\
  vkm_usvec4*: vkm_usvec4_gt,\
  vkm_ivec4*: vkm_ivec4_gt,\
  vkm_uvec4*: vkm_uvec4_gt,\
  vkm_lvec4*: vkm_lvec4_gt,\
  vkm_ulvec4*: vkm_ulvec4_gt,\
  vkm_vec4*: vkm_vec4_gt,\
  vkm_dvec4*: vkm_dvec4_gt,\
  const vkm_bvec2*: vkm_bvec2_gt,\
  const vkm_ubvec2*: vkm_ubvec2_gt,\
  const vkm_svec2*: vkm_svec2_gt,\
  const vkm_usvec2*: vkm_usvec2_gt,\
  const vkm_ivec2*: vkm_ivec2_gt,\
  const vkm_uvec2*: vkm_uvec2_gt,\
  const vkm_lvec2*: vkm_lvec2_gt,\
  const vkm_ulvec2*: vkm_ulvec2_gt,\
  const vkm_vec2*: vkm_vec2_gt,\
  const vkm_dvec2*: vkm_dvec2_gt,\
  const vkm_bvec3*: vkm_bvec3_gt,\
  const vkm_ubvec3*: vkm_ubvec3_gt,\
  const vkm_svec3*: vkm_svec3_gt,\
  const vkm_usvec3*: vkm_usvec3_gt,\
  const vkm_ivec3*: vkm_ivec3_gt,\
  const vkm_uvec3*: vkm_uvec3_gt,\
  const vkm_lvec3*: vkm_lvec3_gt,\
  const vkm_ulvec3*: vkm_ulvec3_gt,\
  const vkm_vec3*: vkm_vec3_gt,\
  const vkm_dvec3*: vkm_dvec3_gt,\
  const vkm_bvec4*: vkm_bvec4_gt,\
  const vkm_ubvec4*: vkm_ubvec4_gt,\
  const vkm_svec4*: vkm_svec4_gt,\
  const vkm_usvec4*: vkm_usvec4_gt,\
  const vkm_ivec4*: vkm_ivec4_gt,\
  const vkm_uvec4*: vkm_uvec4_gt,\
  const vkm_lvec4*: vkm_lvec4_gt,\
  const vkm_ulvec4*: vkm_ulvec4_gt,\
  const vkm_vec4*: vkm_vec4_gt,\
  const vkm_dvec4*: vkm_dvec4_gt\
)(a, __VA_ARGS__)

#define vkm_le(a, ...) _Generic(a,\
  vkm_bvec2*: vkm_bvec2_le,\
  vkm_ubvec2*: vkm_ubvec2_le,\
  vkm_svec2*: vkm_svec2_le,\
  vkm_usvec2*: vkm_usvec2_le,\
  vkm_ivec2*: vkm_ivec2_le,\
  vkm_uvec2*: vkm_uvec2_le,\
  vkm_lvec2*: vkm_lvec2_le,\
  vkm_ulvec2*: vkm_ulvec2_le,\
  vkm_vec2*: vkm_vec2_le,\
  vkm_dvec2*: vkm_dvec2_le,\
  vkm_bvec3*: vkm_bvec3_le,\
  vkm_ubvec3*: vkm_ubvec3_le,\
  vkm_svec3*: vkm_svec3_le,\
  vkm_usvec3*: vkm_usvec3_le,\
  vkm_ivec3*: vkm_ivec3_le,\
  vkm_uvec3*: vkm_uvec3_le,\
  vkm_lvec3*: vkm_lvec3_le,\
  vkm_ulvec3*: vkm_ulvec3_le,\
  vkm_vec3*: vkm_vec3_le,\
  vkm_dvec3*: vkm_dvec3_le,\
  vkm_bvec4*: vkm_bvec4_le,\
  vkm_ubvec4*: vkm_ubvec4_le,\
  vkm_svec4*: vkm_svec4_le,\
  vkm_usvec4*: vkm_usvec4_le,\
  vkm_ivec4*: vkm_ivec4_le,\
  vkm_uvec4*: vkm_uvec4_le,\
  vkm_lvec4*: vkm_lvec4_le,\
  vkm_ulvec4*: vkm_ulvec4_le,\
  vkm_vec4*: vkm_vec4_le,\
  vkm_dvec4*: vkm_dvec4_le,\
  const vkm_bvec2*: vkm_bvec2_le,\
  const vkm_ubvec2*: vkm_ubvec2_le,\
  const vkm_svec2*: vkm_svec2_le,\
  const vkm_usvec2*: vkm_usvec2_le,\
  const vkm_ivec2*: vkm_ivec2_le,\
  const vkm_uvec2*: vkm_uvec2_le,\
  const vkm_lvec2*: vkm_lvec2_le,\
  const vkm_ulvec2*: vkm_ulvec2_le,\
  const vkm_vec2*: vkm_vec2_le,\
  const vkm_dvec2*: vkm_dvec2_le,\
  const vkm_bvec3*: vkm_bvec3_le,\
  const vkm_ubvec3*: vkm_ubvec3_le,\
  const vkm_svec3*: vkm_svec3_le,\
  const vkm_usvec3*: vkm_usvec3_le,\
  const vkm_ivec3*: vkm_ivec3_le,\
  const vkm_uvec3*: vkm_uvec3_le,\
  const vkm_lvec3*: vkm_lvec3_le,\
  const vkm_ulvec3*: vkm_ulvec3_le,\
  const vkm_vec3*: vkm_vec3_le,\
  const vkm_dvec3*: vkm_dvec3_le,\
  const vkm_bvec4*: vkm_bvec4_le,\
  const vkm_ubvec4*: vkm_ubvec4_le,\
  const vkm_svec4*: vkm_svec4_le,\
  const vkm_usvec4*: vkm_usvec4_le,\
  const vkm_ivec4*: vkm_ivec4_le,\
  const vkm_uvec4*: vkm_uvec4_le,\
  const vkm_lvec4*: vkm_lvec4_le,\
  const vkm_ulvec4*: vkm_ulvec4_le,\
  const vkm_vec4*: vkm_vec4_le,\
  const vkm_dvec4*: vkm_dvec4_le\
)(a, __VA_ARGS__)

#define vkm_ge(a, ...) _Generic(a,\
  vkm_bvec2*: vkm_bvec2_ge,\
  vkm_ubvec2*: vkm_ubvec2_ge,\
  vkm_svec2*: vkm_svec2_ge,\
  vkm_usvec2*: vkm_usvec2_ge,\
  vkm_ivec2*: vkm_ivec2_ge,\
  vkm_uvec2*: vkm_uvec2_ge,\
  vkm_lvec2*: vkm_lvec2_ge,\
  vkm_ulvec2*: vkm_ulvec2_ge,\
  vkm_vec2*: vkm_vec2_ge,\
  vkm_dvec2*: vkm_dvec2_ge,\
  vkm_bvec3*: vkm_bvec3_ge,\
  vkm_ubvec3*: vkm_ubvec3_ge,\
  vkm_svec3*: vkm_svec3_ge,\
  vkm_usvec3*: vkm_usvec3_ge,\
  vkm_ivec3*: vkm_ivec3_ge,\
  vkm_uvec3*: vkm_uvec3_ge,\
  vkm_lvec3*: vkm_lvec3_ge,\
  vkm_ulvec3*: vkm_ulvec3_ge,\
  vkm_vec3*: vkm_vec3_ge,\
  vkm_dvec3*: vkm_dvec3_ge,\
  vkm_bvec4*: vkm_bvec4_ge,\
  vkm_ubvec4*: vkm_ubvec4_ge,\
  vkm_svec4*: vkm_svec4_ge,\
  vkm_usvec4*: vkm_usvec4_ge,\
  vkm_ivec4*: vkm_ivec4_ge,\
  vkm_uvec4*: vkm_uvec4_ge,\
  vkm_lvec4*: vkm_lvec4_ge,\
  vkm_ulvec4*: vkm_ulvec4_ge,\
  vkm_vec4*: vkm_vec4_ge,\
  vkm_dvec4*: vkm_dvec4_ge,\
  const vkm_bvec2*: vkm_bvec2_ge,\
  const vkm_ubvec2*: vkm_ubvec2_ge,\
  const vkm_svec2*: vkm_svec2_ge,\
  const vkm_usvec2*: vkm_usvec2_ge,\
  const vkm_ivec2*: vkm_ivec2_ge,\
  const vkm_uvec2*: vkm_uvec2_ge,\
  const vkm_lvec2*: vkm_lvec2_ge,\
  const vkm_ulvec2*: vkm_ulvec2_ge,\
  const vkm_vec2*: vkm_vec2_ge,\
  const vkm_dvec2*: vkm_dvec2_ge,\
  const vkm_bvec3*: vkm_bvec3_ge,\
  const vkm_ubvec3*: vkm_ubvec3_ge,\
  const vkm_svec3*: vkm_svec3_ge,\
  const vkm_usvec3*: vkm_usvec3_ge,\
  const vkm_ivec3*: vkm_ivec3_ge,\
  const vkm_uvec3*: vkm_uvec3_ge,\
  const vkm_lvec3*: vkm_lvec3_ge,\
  const vkm_ulvec3*: vkm_ulvec3_ge,\
  const vkm_vec3*: vkm_vec3_ge,\
  const vkm_dvec3*: vkm_dvec3_ge,\
  const vkm_bvec4*: vkm_bvec4_ge,\
  const vkm_ubvec4*: vkm_ubvec4_ge,\
  const vkm_svec4*: vkm_svec4_ge,\
  const vkm_usvec4*: vkm_usvec4_ge,\
  const vkm_ivec4*: vkm_ivec4_ge,\
  const vkm_uvec4*: vkm_uvec4_ge,\
  const vkm_lvec4*: vkm_lvec4_ge,\
  const vkm_ulvec4*: vkm_ulvec4_ge,\
  const vkm_vec4*: vkm_vec4_ge,\
  const vkm_dvec4*: vkm_dvec4_ge\
)(a, __VA_ARGS__)

static void vkm_orthogonal_lh_zo(
  const float left,
  const float right,
  const float bottom,
  const float top,
  const float near_z,
  const float far_z,
  vkm_mat4* result
) {
  const float x_reciprocal = 1.0f / (right - left);
  const float y_reciprocal = 1.0f / (top - bottom);
  const float z_reciprocal = 1.0f / (far_z - near_z);

  // @formatter:off
  *result = (vkm_mat4){ .raw = {
    2.0f * x_reciprocal,            0.0f,                           0.0f,                   0.0f,
    0.0f,                           2.0f * y_reciprocal,            0.0f,                   0.0f,
    0.0f,                           0.0f,                           z_reciprocal,           0.0f,
    -(right + left) * x_reciprocal, -(top + bottom) * y_reciprocal, near_z * -z_reciprocal, 1.0f,
  }};
  // @formatter:on
}

static void vkm_orthogonal_lh_no(
  const float left,
  const float right,
  const float bottom,
  const float top,
  const float near_z,
  const float far_z,
  vkm_mat4* result
) {
  const float x_reciprocal = 1.0f / (right - left);
  const float y_reciprocal = 1.0f / (top - bottom);
  const float z_reciprocal = 1.0f / (far_z - near_z);

  // @formatter:off
  *result = (vkm_mat4){ .raw = {
    2.0f * x_reciprocal,            0.0f,                           0.0f,                             0.0f,
    0.0f,                           2.0f * y_reciprocal,            0.0f,                             0.0f,
    0.0f,                           0.0f,                           2.0f * z_reciprocal,              0.0f,
    -(right + left) * x_reciprocal, -(top + bottom) * y_reciprocal, (far_z + near_z) * -z_reciprocal, 1.0f,
  }};
  // @formatter:on
}

static void vkm_orthogonal_rh_zo(
  const float left,
  const float right,
  const float bottom,
  const float top,
  const float near_z,
  const float far_z,
  vkm_mat4* result
) {
  const float x_reciprocal = 1.0f / (right - left);
  const float y_reciprocal = 1.0f / (top - bottom);
  const float z_reciprocal = -1.0f / (far_z - near_z);

  // @formatter:off
  *result = (vkm_mat4){ .raw = {
    2.0f * x_reciprocal,            0.0f,                           0.0f,                  0.0f,
    0.0f,                           2.0f * y_reciprocal,            0.0f,                  0.0f,
    0.0f,                           0.0f,                           z_reciprocal,          0.0f,
    -(right + left) * x_reciprocal, -(top + bottom) * y_reciprocal, near_z * z_reciprocal, 1.0f,
  }};
  // @formatter:on
}

static void vkm_orthogonal_rh_no(
  const float left,
  const float right,
  const float bottom,
  const float top,
  const float near_z,
  const float far_z,
  vkm_mat4* result
) {
  const float x_reciprocal = 1.0f / (right - left);
  const float y_reciprocal = 1.0f / (top - bottom);
  const float z_reciprocal = -1.0f / (far_z - near_z);

  // @formatter:off
  *result = (vkm_mat4){ .raw = {
    2.0f * x_reciprocal,            0.0f,                           0.0f,                            0.0f,
    0.0f,                           2.0f * y_reciprocal,            0.0f,                            0.0f,
    0.0f,                           0.0f,                           2.0f * z_reciprocal,             0.0f,
    -(right + left) * x_reciprocal, -(top + bottom) * y_reciprocal, (far_z + near_z) * z_reciprocal, 1.0f,
  }};
  // @formatter:on
}

#ifdef CVKM_LH_ZO
#define vkm_orthogonal vkm_orthogonal_lh_zo
#elif defined(CVKM_LH_NO)
#define vkm_orthogonal vkm_orthogonal_lh_no
#elif defined(CVKM_RH_ZO)
#define vkm_orthogonal vkm_orthogonal_rh_zo
#elif defined(CVKM_RH_NO)
#define vkm_orthogonal vkm_orthogonal_rh_no
#endif

static void vkm_perspective_lh_zo(
  const float field_of_view,
  const float aspect_ratio,
  const float near_plane,
  const float far_plane,
  vkm_mat4* result
) {
  const float focal_length = 1.0f / vkm_tan(field_of_view * 0.5f);
  const float depth_normalization_factor = 1.0f / (near_plane - far_plane);

  *result = (vkm_mat4){
    .m00 = focal_length / aspect_ratio,
    .m11 = focal_length,
    .m22 = -far_plane * depth_normalization_factor,
    .m23 = 1.0f,
    .m32 = near_plane * far_plane * depth_normalization_factor,
  };
}

static void vkm_perspective_lh_no(
  const float field_of_view,
  const float aspect_ratio,
  const float near_plane,
  const float far_plane,
  vkm_mat4* result
) {
  const float focal_length = 1.0f / vkm_tan(field_of_view * 0.5f);
  const float depth_normalization_factor = 1.0f / (near_plane - far_plane);

  *result = (vkm_mat4){
    .m00 = focal_length / aspect_ratio,
    .m11 = focal_length,
    .m22 = -(near_plane + far_plane) * depth_normalization_factor,
    .m23 = 1.0f,
    .m32 = 2.0f * near_plane * far_plane * depth_normalization_factor,
  };
}

static void vkm_perspective_rh_zo(
  float field_of_view,
  float aspect_ratio,
  float near_plane,
  float far_plane,
  vkm_mat4* result
) {
  const float focal_length = 1.0f / vkm_tan(field_of_view * 0.5f);
  const float depth_normalization_factor = 1.0f / (near_plane - far_plane);

  *result = (vkm_mat4){
    .m00 = focal_length / aspect_ratio,
    .m11 = focal_length,
    .m22 = far_plane * depth_normalization_factor,
    .m23 = -1.0f,
    .m32 = near_plane * far_plane * depth_normalization_factor,
  };
}

static void vkm_perspective_rh_no(
  float field_of_view,
  float aspect_ratio,
  float near_plane,
  float far_plane,
  vkm_mat4* result
) {
  const float focal_length = 1.0f / vkm_tan(field_of_view * 0.5f);
  const float depth_normalization_factor = 1.0f / (near_plane - far_plane);

  *result = (vkm_mat4){
    .m00 = focal_length / aspect_ratio,
    .m11 = focal_length,
    .m22 = (near_plane + far_plane) * depth_normalization_factor,
    .m23 = -1.0f,
    .m32 = 2.0f * near_plane * far_plane * depth_normalization_factor,
  };
}

#ifdef CVKM_LH_ZO
#define vkm_perspective vkm_perspective_lh_zo
#elif defined(CVKM_LH_NO)
#define vkm_perspective vkm_perspective_lh_no
#elif defined(CVKM_RH_ZO)
#define vkm_perspective vkm_perspective_rh_zo
#elif defined(CVKM_RH_NO)
#define vkm_perspective vkm_perspective_rh_no
#endif

static void vkm_mat4_mul(const vkm_mat4* a, const vkm_mat4* b, vkm_mat4* result) {
  const vkm_mat4 a_copy = *a, b_copy = *b;

  result->m00 = a_copy.m00 * b_copy.m00 + a_copy.m10 * b_copy.m01 + a_copy.m20 * b_copy.m02 + a_copy.m30 * b_copy.m03;
  result->m01 = a_copy.m01 * b_copy.m00 + a_copy.m11 * b_copy.m01 + a_copy.m21 * b_copy.m02 + a_copy.m31 * b_copy.m03;
  result->m02 = a_copy.m02 * b_copy.m00 + a_copy.m12 * b_copy.m01 + a_copy.m22 * b_copy.m02 + a_copy.m32 * b_copy.m03;
  result->m03 = a_copy.m03 * b_copy.m00 + a_copy.m13 * b_copy.m01 + a_copy.m23 * b_copy.m02 + a_copy.m33 * b_copy.m03;

  result->m10 = a_copy.m00 * b_copy.m10 + a_copy.m10 * b_copy.m11 + a_copy.m20 * b_copy.m12 + a_copy.m30 * b_copy.m13;
  result->m11 = a_copy.m01 * b_copy.m10 + a_copy.m11 * b_copy.m11 + a_copy.m21 * b_copy.m12 + a_copy.m31 * b_copy.m13;
  result->m12 = a_copy.m02 * b_copy.m10 + a_copy.m12 * b_copy.m11 + a_copy.m22 * b_copy.m12 + a_copy.m32 * b_copy.m13;
  result->m13 = a_copy.m03 * b_copy.m10 + a_copy.m13 * b_copy.m11 + a_copy.m23 * b_copy.m12 + a_copy.m33 * b_copy.m13;

  result->m20 = a_copy.m00 * b_copy.m20 + a_copy.m10 * b_copy.m21 + a_copy.m20 * b_copy.m22 + a_copy.m30 * b_copy.m23;
  result->m21 = a_copy.m01 * b_copy.m20 + a_copy.m11 * b_copy.m21 + a_copy.m21 * b_copy.m22 + a_copy.m31 * b_copy.m23;
  result->m22 = a_copy.m02 * b_copy.m20 + a_copy.m12 * b_copy.m21 + a_copy.m22 * b_copy.m22 + a_copy.m32 * b_copy.m23;
  result->m23 = a_copy.m03 * b_copy.m20 + a_copy.m13 * b_copy.m21 + a_copy.m23 * b_copy.m22 + a_copy.m33 * b_copy.m23;

  result->m30 = a_copy.m00 * b_copy.m30 + a_copy.m10 * b_copy.m31 + a_copy.m20 * b_copy.m32 + a_copy.m30 * b_copy.m33;
  result->m31 = a_copy.m01 * b_copy.m30 + a_copy.m11 * b_copy.m31 + a_copy.m21 * b_copy.m32 + a_copy.m31 * b_copy.m33;
  result->m32 = a_copy.m02 * b_copy.m30 + a_copy.m12 * b_copy.m31 + a_copy.m22 * b_copy.m32 + a_copy.m32 * b_copy.m33;
  result->m33 = a_copy.m03 * b_copy.m30 + a_copy.m13 * b_copy.m31 + a_copy.m23 * b_copy.m32 + a_copy.m33 * b_copy.m33;
}

static void vkm_mat4_mul_transform(const vkm_mat4* a, const vkm_mat4* b, vkm_mat4* result) {
  const vkm_mat4 a_copy = *a, b_copy = *b;

  result->m00 = a_copy.m00 * b_copy.m00 + a_copy.m10 * b_copy.m01 + a_copy.m20 * b_copy.m02;
  result->m01 = a_copy.m01 * b_copy.m00 + a_copy.m11 * b_copy.m01 + a_copy.m21 * b_copy.m02;
  result->m02 = a_copy.m02 * b_copy.m00 + a_copy.m12 * b_copy.m01 + a_copy.m22 * b_copy.m02;
  result->m03 = a_copy.m03 * b_copy.m00 + a_copy.m13 * b_copy.m01 + a_copy.m23 * b_copy.m02;

  result->m10 = a_copy.m00 * b_copy.m10 + a_copy.m10 * b_copy.m11 + a_copy.m20 * b_copy.m12;
  result->m11 = a_copy.m01 * b_copy.m10 + a_copy.m11 * b_copy.m11 + a_copy.m21 * b_copy.m12;
  result->m12 = a_copy.m02 * b_copy.m10 + a_copy.m12 * b_copy.m11 + a_copy.m22 * b_copy.m12;
  result->m13 = a_copy.m03 * b_copy.m10 + a_copy.m13 * b_copy.m11 + a_copy.m23 * b_copy.m12;

  result->m20 = a_copy.m00 * b_copy.m20 + a_copy.m10 * b_copy.m21 + a_copy.m20 * b_copy.m22;
  result->m21 = a_copy.m01 * b_copy.m20 + a_copy.m11 * b_copy.m21 + a_copy.m21 * b_copy.m22;
  result->m22 = a_copy.m02 * b_copy.m20 + a_copy.m12 * b_copy.m21 + a_copy.m22 * b_copy.m22;
  result->m23 = a_copy.m03 * b_copy.m20 + a_copy.m13 * b_copy.m21 + a_copy.m23 * b_copy.m22;

  result->m30 = a_copy.m00 * b_copy.m30 + a_copy.m10 * b_copy.m31 + a_copy.m20 * b_copy.m32 + a_copy.m30 * b_copy.m33;
  result->m31 = a_copy.m01 * b_copy.m30 + a_copy.m11 * b_copy.m31 + a_copy.m21 * b_copy.m32 + a_copy.m31 * b_copy.m33;
  result->m32 = a_copy.m02 * b_copy.m30 + a_copy.m12 * b_copy.m31 + a_copy.m22 * b_copy.m32 + a_copy.m32 * b_copy.m33;
  result->m33 = a_copy.m03 * b_copy.m30 + a_copy.m13 * b_copy.m31 + a_copy.m23 * b_copy.m32 + a_copy.m33 * b_copy.m33;
}

static void vkm_mat4_mul_rotation(const vkm_mat4* a, const vkm_mat4* b, vkm_mat4* result) {
  const vkm_mat4 a_copy = *a;
  const float
    b00 = b->m00, b01 = b->m01, b02 = b->m02,
    b10 = b->m10, b11 = b->m11, b12 = b->m12,
    b20 = b->m20, b21 = b->m21, b22 = b->m22;

  result->m00 = a_copy.m00 * b00 + a_copy.m10 * b01 + a_copy.m20 * b02;
  result->m01 = a_copy.m01 * b00 + a_copy.m11 * b01 + a_copy.m21 * b02;
  result->m02 = a_copy.m02 * b00 + a_copy.m12 * b01 + a_copy.m22 * b02;
  result->m03 = a_copy.m03 * b00 + a_copy.m13 * b01 + a_copy.m23 * b02;

  result->m10 = a_copy.m00 * b10 + a_copy.m10 * b11 + a_copy.m20 * b12;
  result->m11 = a_copy.m01 * b10 + a_copy.m11 * b11 + a_copy.m21 * b12;
  result->m12 = a_copy.m02 * b10 + a_copy.m12 * b11 + a_copy.m22 * b12;
  result->m13 = a_copy.m03 * b10 + a_copy.m13 * b11 + a_copy.m23 * b12;

  result->m20 = a_copy.m00 * b20 + a_copy.m10 * b21 + a_copy.m20 * b22;
  result->m21 = a_copy.m01 * b20 + a_copy.m11 * b21 + a_copy.m21 * b22;
  result->m22 = a_copy.m02 * b20 + a_copy.m12 * b21 + a_copy.m22 * b22;
  result->m23 = a_copy.m03 * b20 + a_copy.m13 * b21 + a_copy.m23 * b22;

  result->m30 = a_copy.m30;
  result->m31 = a_copy.m31;
  result->m32 = a_copy.m32;
  result->m33 = a_copy.m33;
}

static void vkm_quat_make_rotation(const float angle, const vkm_vec3* axis, vkm_versor* result) {
  vkm_vec3 normalized_axis;
  vkm_normalize(axis, &normalized_axis);

  const float half_angle = angle * 0.5f;
  const float sine = sinf(half_angle);
  *result = (vkm_versor){ {
    axis->x * sine,
    axis->y * sine,
    axis->z * sine,
    vkm_cos(half_angle),
  } };
}

static void vkm_mat4_make_rotation(const float angle, const vkm_vec3* axis, vkm_mat4* result) {
  vkm_vec3 axis_normalized = *axis;
  vkm_normalize(axis, &axis_normalized);
  const float cosine = cosf(angle);
  vkm_vec3 vec_cosine, vec_sine;

  vkm_mul(&axis_normalized, 1.0f - cosine, &vec_cosine);
  vkm_mul(&axis_normalized, sinf(angle), &vec_sine);

  vkm_mul(&axis_normalized, vec_cosine.x, (vkm_vec3*)result->columns);
  vkm_mul(&axis_normalized, vec_cosine.y, (vkm_vec3*)(result->columns + 1));
  vkm_mul(&axis_normalized, vec_cosine.z, (vkm_vec3*)(result->columns + 2));

  // @formatter:off
  result->m00 += cosine;     result->m10 -= vec_sine.z; result->m20 += vec_sine.y;
  result->m01 += vec_sine.z; result->m11 += cosine;     result->m21 -= vec_sine.x;
  result->m02 -= vec_sine.y; result->m12 += vec_sine.x; result->m22 += cosine;

  result->m03 = result->m13 = result->m23 = result->m30 = result->m31 = result->m32 = 0.0f;
  result->m33 = 1.0f;
  // @formatter:on
}

#define vkm_make_rotation(angle, axis, result) _Generic(result,\
  vkm_versor*: vkm_quat_make_rotation,\
  vkm_mat4*: vkm_mat4_make_rotation\
)(angle, axis, result)

static void vkm_translate_vec2(vkm_mat4* matrix, const vkm_vec2* translation) {
  vkm_muladd(matrix->columns, translation->x, matrix->columns + 3);
  vkm_muladd(matrix->columns + 1, translation->y, matrix->columns + 3);
}

static void vkm_translate_vec3(vkm_mat4* matrix, const vkm_vec3* translation) {
  vkm_muladd(matrix->columns, translation->x, matrix->columns + 3);
  vkm_muladd(matrix->columns + 1, translation->y, matrix->columns + 3);
  vkm_muladd(matrix->columns + 2, translation->z, matrix->columns + 3);
}

#define vkm_translate(matrix, ...) _Generic(__VA_ARGS__,\
  vkm_vec2*: vkm_translate_vec2,\
  vkm_vec3*: vkm_translate_vec3,\
  const vkm_vec2*: vkm_translate_vec2,\
  const vkm_vec3*: vkm_translate_vec3\
)(matrix, __VA_ARGS__)

static void vkm_rotate(vkm_mat4* matrix, const float angle, const vkm_vec3* axis) {
  vkm_mat4 rotation;
  vkm_make_rotation(angle, axis, &rotation);
  vkm_mat4_mul_rotation(matrix, &rotation, matrix);
}

static void vkm_scale(vkm_mat4* matrix, const vkm_vec3* vector) {
  vkm_mul(matrix->columns, vector->x, matrix->columns);
  vkm_mul(matrix->columns + 1, vector->y, matrix->columns + 1);
  vkm_mul(matrix->columns + 2, vector->z, matrix->columns + 2);
}

static void vkm_quat_to_mat4(const vkm_versor* versor, vkm_mat4* result) {
  const float sqr_magnitude = vkm_sqr_magnitude(versor);
  const float scale_factor = sqr_magnitude > 0.0f ? 2.0f / sqr_magnitude : 0.0f;

  const float xx = scale_factor * versor->x * versor->x;
  const float yy = scale_factor * versor->y * versor->y;
  const float zz = scale_factor * versor->z * versor->z;

  const float xy = scale_factor * versor->x * versor->y;
  const float yz = scale_factor * versor->y * versor->z;
  const float xz = scale_factor * versor->x * versor->z;

  const float wx = scale_factor * versor->w * versor->x;
  const float wy = scale_factor * versor->w * versor->y;
  const float wz = scale_factor * versor->w * versor->z;

  result->m00 = 1.0f - yy - zz;
  result->m11 = 1.0f - xx - zz;
  result->m22 = 1.0f - xx - yy;

  result->m01 = xy + wz;
  result->m12 = yz + wx;
  result->m20 = xz + wy;

  result->m10 = xy - wz;
  result->m21 = yz - wx;
  result->m02 = xz - wy;

  result->m03 = result->m13 = result->m23 = result->m30 = result->m31 = result->m32 = 0.0f;
  result->m33 = 1.0f;
}

static void vkm_mat4_to_euler(const vkm_mat4* matrix, vkm_vec3* result) {
  if (matrix->m20 > -1.0f && matrix->m20 < 1.0f) {
    // There's a single Euler representation, all good.
    result->y = vkm_asin(matrix->m20);
    result->x = vkm_atan2(-matrix->m21, matrix->m22);
    result->z = vkm_atan2(-matrix->m10, matrix->m00);
    return;
  }

  // Gimbal lock: m20 is either exactly 1 or -1, there's multiple correct answers!
  result->y = CVKM_PI_2_F * matrix->m20;
  result->x = vkm_atan2(matrix->m01, matrix->m11) * matrix->m20;
  result->z = 0.0f; // Arbitrarily set to 0.
}

static void vkm_euler_to_mat4(const vkm_vec3* euler, vkm_mat4* result) {
  const float x_sine = vkm_sin(euler->x);
  const float x_cosine = vkm_cos(euler->x);
  const float y_sine = vkm_sin(euler->y);
  const float y_cosine = vkm_cos(euler->y);
  const float z_sine = vkm_sin(euler->z);
  const float z_cosine = vkm_cos(euler->z);

  const float z_cosine_times_x_sine = z_cosine * x_sine;
  const float x_cosine_times_z_cosine = x_cosine * z_cosine;
  const float y_sine_times_z_sine = y_sine * z_sine;

  // Set the rotation portion of the matrix.
  result->m00 = y_cosine * z_cosine;
  result->m01 = z_cosine_times_x_sine * y_sine + x_cosine * z_sine;
  result->m02 = -x_cosine_times_z_cosine * y_sine + x_sine * z_sine;
  result->m10 = -y_cosine * z_sine;
  result->m11 = x_cosine_times_z_cosine - x_sine * y_sine_times_z_sine;
  result->m12 = z_cosine_times_x_sine + x_cosine * y_sine_times_z_sine;
  result->m20 = y_sine;
  result->m21 = -y_cosine * x_sine;
  result->m22 = x_cosine * y_cosine;

  // Set the rest of the matrix to identity values.
  result->columns[3] = (vkm_vec4){ { 0.0f, 0.0f, 0.0f, 1.0f } };
  result->m30 = 0.0f;
  result->m31 = 0.0f;
  result->m32 = 0.0f;
}

static void vkm_euler_to_quat_lh(const vkm_vec3* euler, vkm_quat* result) {
  const float x_sine = vkm_sin(euler->x * 0.5f);
  const float x_cosine = vkm_cos(euler->x * 0.5f);
  const float y_sine = vkm_sin(euler->y * 0.5f);
  const float y_cosine = vkm_cos(euler->y * 0.5f);
  const float z_sine = -vkm_sin(euler->z * 0.5f);
  const float z_cosine = vkm_cos(euler->z * 0.5f);

  *result = (vkm_quat){ {
    x_cosine * y_sine * z_sine + x_sine * y_cosine * z_cosine,
    x_cosine * y_sine * z_cosine - x_sine * y_cosine * z_sine,
    x_cosine * y_cosine * z_sine + x_sine * y_sine * z_cosine,
    x_cosine * y_cosine * z_cosine - x_sine * y_sine * z_sine,
  } };
}

static void vkm_euler_to_quat_rh(const vkm_vec3* euler, vkm_versor* result) {
  const float x_sine = vkm_sin(euler->x * 0.5f);
  const float x_cosine = vkm_cos(euler->x * 0.5f);
  const float y_sine = vkm_sin(euler->y * 0.5f);
  const float y_cosine = vkm_cos(euler->y * 0.5f);
  const float z_sine = vkm_sin(euler->z * 0.5f);
  const float z_cosine = vkm_cos(euler->z * 0.5f);

  *result = (vkm_quat){ {
    x_cosine * y_sine * z_sine + x_sine * y_cosine * z_cosine,
    x_cosine * y_sine * z_cosine - x_sine * y_cosine * z_sine,
    x_cosine * y_cosine * z_sine + x_sine * y_sine * z_cosine,
    x_cosine * y_cosine * z_cosine - x_sine * y_sine * z_sine,
  } };
}

static void vkm_mat4_to_mat3(const vkm_mat4* matrix, vkm_mat3* result) {
  *result = (vkm_mat3){
    .m00 = matrix->m00, .m01 = matrix->m01, .m02 = matrix->m02,
    .m10 = matrix->m10, .m11 = matrix->m11, .m12 = matrix->m12,
    .m20 = matrix->m20, .m21 = matrix->m21, .m22 = matrix->m22,
  };
}

static void vkm_mat3_to_quat(const vkm_mat3* matrix, vkm_quat* result) {
  const float trace = matrix->m00 + matrix->m11 + matrix->m22;
  if (trace >= 0.0f) {
    const float root = vkm_sqrt(1.0f + trace);
    const float inverse_root = 0.5f / root;

    *result = (vkm_quat){ {
      inverse_root * (matrix->m12 - matrix->m21),
      inverse_root * (matrix->m20 - matrix->m02),
      inverse_root * (matrix->m01 - matrix->m10),
      root * 0.5f,
    } };
  } else if (matrix->m00 >= matrix->m11 && matrix->m00 >= matrix->m22) {
    const float root = vkm_sqrt(1.0f - matrix->m11 - matrix->m22 + matrix->m00);
    const float inverse_root = 0.5f / root;

    *result = (vkm_quat){ {
      root * 0.5f,
      inverse_root * (matrix->m01 + matrix->m10),
      inverse_root * (matrix->m02 + matrix->m20),
      inverse_root * (matrix->m12 - matrix->m21),
    } };
  } else if (matrix->m11 >= matrix->m22) {
    const float root = vkm_sqrt(1.0f - matrix->m00 - matrix->m22 + matrix->m11);
    const float inverse_root = 0.5f / root;
    *result = (vkm_quat){ {
      inverse_root * (matrix->m01 + matrix->m10),
      root * 0.5f,
      inverse_root * (matrix->m12 + matrix->m21),
      inverse_root * (matrix->m20 - matrix->m02),
    } };
  } else {
    const float root = vkm_sqrt(1.0f - matrix->m00 - matrix->m11 + matrix->m22);
    const float inverse_root = 0.5f / root;

    *result = (vkm_quat){ {
      inverse_root * (matrix->m02 + matrix->m20),
      inverse_root * (matrix->m12 + matrix->m21),
      root * 0.5f,
      inverse_root * (matrix->m01 - matrix->m10),
    } };
  }
}

// Direction must be normalized.
static void vkm_look_rotation_lh(const vkm_vec3* direction, const vkm_vec3* up, vkm_versor* result) {
  vkm_mat3 matrix = CVKM_MAT3_IDENTITY;

  matrix.columns[2] = *direction;

  vkm_vec3 right;
  vkm_cross(up, matrix.columns + 2, &right);

  vkm_mul(&right, vkm_inverse_sqrt(vkm_max(1e-30f, vkm_sqr_magnitude(&right))), matrix.columns);
  vkm_cross(matrix.columns + 2, matrix.columns, matrix.columns + 1);

  vkm_mat3_to_quat(&matrix, result);
}

// Direction must be normalized.
static void vkm_look_rotation_rh(const vkm_vec3* direction, const vkm_vec3* up, vkm_versor* result) {
  vkm_mat3 matrix = CVKM_MAT3_IDENTITY;

  vkm_invert(direction, matrix.columns + 2);

  vkm_vec3 right;
  vkm_cross(up, matrix.columns + 2, &right);

  vkm_mul(&right, vkm_inverse_sqrt(vkm_max(1e-30f, vkm_sqr_magnitude(&right))), matrix.columns);
  vkm_cross(matrix.columns + 2, matrix.columns, matrix.columns + 1);

  vkm_mat3_to_quat(&matrix, result);
}

static void vkm_look_at_lh(const vkm_vec3* eye, const vkm_vec3* target, const vkm_vec3* up, vkm_mat4* result) {
  vkm_vec3 forward, right, true_up;

  vkm_vec3_sub(target, eye, &forward);
  vkm_vec3_normalize(&forward, &forward);

  vkm_vec3_cross(up, &forward, &right);
  vkm_vec3_normalize(&right, &right);
  vkm_vec3_cross(&forward, &right, &true_up);

  // @formatter:off
  result->m00 = right.x; result->m01 = true_up.x; result->m02 = forward.x;
  result->m10 = right.y; result->m11 = true_up.y; result->m12 = forward.y;
  result->m20 = right.z; result->m21 = true_up.z; result->m22 = forward.z;

  result->m30 = -vkm_vec3_dot(&right, eye);
  result->m31 = -vkm_vec3_dot(&true_up, eye);
  result->m32 = -vkm_vec3_dot(&forward, eye);

  result->m03 = result->m13 = result->m23 = 0.0f;
  result->m33 = 1.0f;
  // @formatter:on
}

static void vkm_look_at_rh(const vkm_vec3* eye, const vkm_vec3* target, const vkm_vec3* up, vkm_mat4* result) {
  vkm_vec3 forward, right, true_up;

  vkm_vec3_sub(target, eye, &forward);
  vkm_vec3_normalize(&forward, &forward);

  vkm_vec3_cross(&forward, up, &right);
  vkm_vec3_normalize(&right, &right);
  vkm_vec3_cross(&right, &forward, &true_up);

  // @formatter:off
  result->m00 = right.x; result->m01 = true_up.x; result->m02 =-forward.x;
  result->m10 = right.y; result->m11 = true_up.y; result->m12 =-forward.y;
  result->m20 = right.z; result->m21 = true_up.z; result->m22 =-forward.z;

  result->m30 = -vkm_vec3_dot(&right, eye);
  result->m31 = -vkm_vec3_dot(&true_up, eye);
  result->m32 =  vkm_vec3_dot(&forward, eye);

  result->m03 = result->m13 = result->m23 = 0.0f;
  result->m33 = 1.0f;
  // @formatter:on
}

#ifdef CVKM_LH
#define vkm_euler_to_quat vkm_euler_to_quat_lh
#define vkm_look_rotation vkm_look_rotation_lh
#define vkm_look_at vkm_look_at_lh
#else
#define vkm_euler_to_quat vkm_euler_to_quat_rh
#define vkm_look_rotation vkm_look_rotation_rh
#define vkm_look_at vkm_look_at_rh
#endif

static void vkm_quat_conjugate(const vkm_quat* quaternion, vkm_quat* result) {
  *result = (vkm_quat){ {
    -quaternion->x,
    -quaternion->y,
    -quaternion->z,
    quaternion->w,
  } };
}

#define vkm_deg2rad(angle) _Generic(angle,\
  float: (angle) * CVKM_DEG2RAD_F,\
  double: (angle) * CVKM_DEG2RAD\
)
#define vkm_rad2deg(angle) _Generic(angle,\
  float: (angle) * CVKM_RAD2DEG_F,\
  double: (angle) * CVKM_RAD2DEG\
)

typedef vkm_vec2 Position2D;
typedef vkm_vec3 Position3D;
typedef vkm_vec4 Position4D;
typedef vkm_dvec2 DoublePosition2D;
typedef vkm_dvec3 DoublePosition3D;
typedef vkm_dvec4 DoublePosition4D;
typedef float Rotation2D;
typedef vkm_versor Rotation3D;
typedef float Scale;
typedef vkm_vec2 Scale2D;
typedef vkm_vec3 Scale3D;
typedef vkm_vec4 Scale4D;
typedef vkm_mat4 Transform;
typedef vkm_vec2 Velocity2D;
typedef vkm_vec3 Velocity3D;
typedef vkm_vec4 Velocity4D;
typedef vkm_vec2 Acceleration2D;
typedef vkm_vec3 Acceleration3D;
typedef vkm_vec4 Acceleration4D;
typedef vkm_vec2 Force2D;
typedef vkm_vec3 Force3D;
typedef vkm_vec4 Force4D;
typedef float Mass;
typedef float Damping;
typedef vkm_vec2 Gravity2D;
typedef vkm_vec3 Gravity3D;
typedef vkm_vec4 Gravity4D;
typedef float GravityScale;

#ifdef CVKM_ENABLE_FLECS
extern ECS_COMPONENT_DECLARE(vkm_bvec2);
extern ECS_COMPONENT_DECLARE(vkm_ubvec2);
extern ECS_COMPONENT_DECLARE(vkm_svec2);
extern ECS_COMPONENT_DECLARE(vkm_usvec2);
extern ECS_COMPONENT_DECLARE(vkm_ivec2);
extern ECS_COMPONENT_DECLARE(vkm_uvec2);
extern ECS_COMPONENT_DECLARE(vkm_lvec2);
extern ECS_COMPONENT_DECLARE(vkm_ulvec2);
extern ECS_COMPONENT_DECLARE(vkm_vec2);
extern ECS_COMPONENT_DECLARE(vkm_dvec2);
extern ECS_COMPONENT_DECLARE(vkm_bvec3);
extern ECS_COMPONENT_DECLARE(vkm_ubvec3);
extern ECS_COMPONENT_DECLARE(vkm_svec3);
extern ECS_COMPONENT_DECLARE(vkm_usvec3);
extern ECS_COMPONENT_DECLARE(vkm_ivec3);
extern ECS_COMPONENT_DECLARE(vkm_uvec3);
extern ECS_COMPONENT_DECLARE(vkm_lvec3);
extern ECS_COMPONENT_DECLARE(vkm_ulvec3);
extern ECS_COMPONENT_DECLARE(vkm_vec3);
extern ECS_COMPONENT_DECLARE(vkm_dvec3);
extern ECS_COMPONENT_DECLARE(vkm_bvec4);
extern ECS_COMPONENT_DECLARE(vkm_ubvec4);
extern ECS_COMPONENT_DECLARE(vkm_svec4);
extern ECS_COMPONENT_DECLARE(vkm_usvec4);
extern ECS_COMPONENT_DECLARE(vkm_ivec4);
extern ECS_COMPONENT_DECLARE(vkm_uvec4);
extern ECS_COMPONENT_DECLARE(vkm_lvec4);
extern ECS_COMPONENT_DECLARE(vkm_ulvec4);
extern ECS_COMPONENT_DECLARE(vkm_vec4);
extern ECS_COMPONENT_DECLARE(vkm_dvec4);
extern ECS_COMPONENT_DECLARE(vkm_mat3);
extern ECS_COMPONENT_DECLARE(vkm_mat4);
extern ECS_COMPONENT_DECLARE(vkm_quat);
extern ECS_COMPONENT_DECLARE(vkm_versor);
extern ECS_COMPONENT_DECLARE(Position2D);
extern ECS_COMPONENT_DECLARE(Position3D);
extern ECS_COMPONENT_DECLARE(Position4D);
extern ECS_COMPONENT_DECLARE(DoublePosition2D);
extern ECS_COMPONENT_DECLARE(DoublePosition3D);
extern ECS_COMPONENT_DECLARE(DoublePosition4D);
extern ECS_COMPONENT_DECLARE(Rotation2D);
extern ECS_COMPONENT_DECLARE(Rotation3D);
extern ECS_COMPONENT_DECLARE(Scale);
extern ECS_COMPONENT_DECLARE(Scale2D);
extern ECS_COMPONENT_DECLARE(Scale3D);
extern ECS_COMPONENT_DECLARE(Scale4D);
extern ECS_COMPONENT_DECLARE(Transform);
extern ECS_COMPONENT_DECLARE(Velocity2D);
extern ECS_COMPONENT_DECLARE(Velocity3D);
extern ECS_COMPONENT_DECLARE(Velocity4D);
extern ECS_COMPONENT_DECLARE(Acceleration2D);
extern ECS_COMPONENT_DECLARE(Acceleration3D);
extern ECS_COMPONENT_DECLARE(Acceleration4D);
extern ECS_COMPONENT_DECLARE(Force2D);
extern ECS_COMPONENT_DECLARE(Force3D);
extern ECS_COMPONENT_DECLARE(Force4D);
extern ECS_COMPONENT_DECLARE(Mass);
extern ECS_COMPONENT_DECLARE(Damping);
extern ECS_COMPONENT_DECLARE(Gravity2D);
extern ECS_COMPONENT_DECLARE(Gravity3D);
extern ECS_COMPONENT_DECLARE(Gravity4D);
extern ECS_COMPONENT_DECLARE(GravityScale);

void cvkmImport(ecs_world_t* world);

#ifdef CVKM_FLECS_IMPLEMENTATION
#include <stddef.h>

ECS_COMPONENT_DECLARE(vkm_bvec2);
ECS_COMPONENT_DECLARE(vkm_ubvec2);
ECS_COMPONENT_DECLARE(vkm_svec2);
ECS_COMPONENT_DECLARE(vkm_usvec2);
ECS_COMPONENT_DECLARE(vkm_ivec2);
ECS_COMPONENT_DECLARE(vkm_uvec2);
ECS_COMPONENT_DECLARE(vkm_lvec2);
ECS_COMPONENT_DECLARE(vkm_ulvec2);
ECS_COMPONENT_DECLARE(vkm_vec2);
ECS_COMPONENT_DECLARE(vkm_dvec2);
ECS_COMPONENT_DECLARE(vkm_bvec3);
ECS_COMPONENT_DECLARE(vkm_ubvec3);
ECS_COMPONENT_DECLARE(vkm_svec3);
ECS_COMPONENT_DECLARE(vkm_usvec3);
ECS_COMPONENT_DECLARE(vkm_ivec3);
ECS_COMPONENT_DECLARE(vkm_uvec3);
ECS_COMPONENT_DECLARE(vkm_lvec3);
ECS_COMPONENT_DECLARE(vkm_ulvec3);
ECS_COMPONENT_DECLARE(vkm_vec3);
ECS_COMPONENT_DECLARE(vkm_dvec3);
ECS_COMPONENT_DECLARE(vkm_bvec4);
ECS_COMPONENT_DECLARE(vkm_ubvec4);
ECS_COMPONENT_DECLARE(vkm_svec4);
ECS_COMPONENT_DECLARE(vkm_usvec4);
ECS_COMPONENT_DECLARE(vkm_ivec4);
ECS_COMPONENT_DECLARE(vkm_uvec4);
ECS_COMPONENT_DECLARE(vkm_lvec4);
ECS_COMPONENT_DECLARE(vkm_ulvec4);
ECS_COMPONENT_DECLARE(vkm_vec4);
ECS_COMPONENT_DECLARE(vkm_dvec4);
ECS_COMPONENT_DECLARE(vkm_mat3);
ECS_COMPONENT_DECLARE(vkm_mat4);
ECS_COMPONENT_DECLARE(vkm_quat);
ECS_COMPONENT_DECLARE(vkm_versor);
ECS_COMPONENT_DECLARE(Position2D);
ECS_COMPONENT_DECLARE(Position3D);
ECS_COMPONENT_DECLARE(Position4D);
ECS_COMPONENT_DECLARE(DoublePosition2D);
ECS_COMPONENT_DECLARE(DoublePosition3D);
ECS_COMPONENT_DECLARE(DoublePosition4D);
ECS_COMPONENT_DECLARE(Rotation2D);
ECS_COMPONENT_DECLARE(Rotation3D);
ECS_COMPONENT_DECLARE(Scale);
ECS_COMPONENT_DECLARE(Scale2D);
ECS_COMPONENT_DECLARE(Scale3D);
ECS_COMPONENT_DECLARE(Scale4D);
ECS_COMPONENT_DECLARE(Transform);
ECS_COMPONENT_DECLARE(Velocity2D);
ECS_COMPONENT_DECLARE(Velocity3D);
ECS_COMPONENT_DECLARE(Velocity4D);
ECS_COMPONENT_DECLARE(Acceleration2D);
ECS_COMPONENT_DECLARE(Acceleration3D);
ECS_COMPONENT_DECLARE(Acceleration4D);
ECS_COMPONENT_DECLARE(Force2D);
ECS_COMPONENT_DECLARE(Force3D);
ECS_COMPONENT_DECLARE(Force4D);
ECS_COMPONENT_DECLARE(Mass);
ECS_COMPONENT_DECLARE(Damping);
ECS_COMPONENT_DECLARE(Gravity2D);
ECS_COMPONENT_DECLARE(Gravity3D);
ECS_COMPONENT_DECLARE(Gravity4D);
ECS_COMPONENT_DECLARE(GravityScale);

#define CVKM_SPAWN_ZERO_CTOR(type) static ECS_CTOR(type, ptr, {\
  *ptr = (type){ 0 };\
})

#define CVKM_SPAWN_ONE_CTOR(type) static ECS_CTOR(type, ptr, {\
  *ptr = (type){ 1 };\
})

CVKM_SPAWN_ZERO_CTOR(vkm_bvec2)
CVKM_SPAWN_ZERO_CTOR(vkm_ubvec2)
CVKM_SPAWN_ZERO_CTOR(vkm_svec2)
CVKM_SPAWN_ZERO_CTOR(vkm_usvec2)
CVKM_SPAWN_ZERO_CTOR(vkm_ivec2)
CVKM_SPAWN_ZERO_CTOR(vkm_uvec2)
CVKM_SPAWN_ZERO_CTOR(vkm_lvec2)
CVKM_SPAWN_ZERO_CTOR(vkm_ulvec2)
CVKM_SPAWN_ZERO_CTOR(vkm_vec2)
CVKM_SPAWN_ZERO_CTOR(vkm_dvec2)
CVKM_SPAWN_ZERO_CTOR(vkm_bvec3)
CVKM_SPAWN_ZERO_CTOR(vkm_ubvec3)
CVKM_SPAWN_ZERO_CTOR(vkm_svec3)
CVKM_SPAWN_ZERO_CTOR(vkm_usvec3)
CVKM_SPAWN_ZERO_CTOR(vkm_ivec3)
CVKM_SPAWN_ZERO_CTOR(vkm_uvec3)
CVKM_SPAWN_ZERO_CTOR(vkm_lvec3)
CVKM_SPAWN_ZERO_CTOR(vkm_ulvec3)
CVKM_SPAWN_ZERO_CTOR(vkm_vec3)
CVKM_SPAWN_ZERO_CTOR(vkm_dvec3)
CVKM_SPAWN_ZERO_CTOR(vkm_bvec4)
CVKM_SPAWN_ZERO_CTOR(vkm_ubvec4)
CVKM_SPAWN_ZERO_CTOR(vkm_svec4)
CVKM_SPAWN_ZERO_CTOR(vkm_usvec4)
CVKM_SPAWN_ZERO_CTOR(vkm_ivec4)
CVKM_SPAWN_ZERO_CTOR(vkm_uvec4)
CVKM_SPAWN_ZERO_CTOR(vkm_lvec4)
CVKM_SPAWN_ZERO_CTOR(vkm_ulvec4)
CVKM_SPAWN_ZERO_CTOR(vkm_vec4)
CVKM_SPAWN_ZERO_CTOR(vkm_dvec4)
CVKM_SPAWN_ZERO_CTOR(Position2D)
CVKM_SPAWN_ZERO_CTOR(Position3D)
CVKM_SPAWN_ZERO_CTOR(Position4D)
CVKM_SPAWN_ZERO_CTOR(DoublePosition2D)
CVKM_SPAWN_ZERO_CTOR(DoublePosition3D)
CVKM_SPAWN_ZERO_CTOR(DoublePosition4D)
CVKM_SPAWN_ZERO_CTOR(Acceleration2D)
CVKM_SPAWN_ZERO_CTOR(Acceleration3D)
CVKM_SPAWN_ZERO_CTOR(Acceleration4D)
CVKM_SPAWN_ZERO_CTOR(Force2D)
CVKM_SPAWN_ZERO_CTOR(Force3D)
CVKM_SPAWN_ZERO_CTOR(Force4D)
CVKM_SPAWN_ZERO_CTOR(Rotation2D)
CVKM_SPAWN_ZERO_CTOR(Velocity2D)
CVKM_SPAWN_ZERO_CTOR(Velocity3D)
CVKM_SPAWN_ZERO_CTOR(Velocity4D)
CVKM_SPAWN_ONE_CTOR(Mass)
CVKM_SPAWN_ONE_CTOR(GravityScale)

ECS_CTOR(vkm_mat3, ptr, {
  *ptr = CVKM_MAT3_IDENTITY;
})

ECS_CTOR(vkm_mat4, ptr, {
  *ptr = CVKM_MAT4_IDENTITY;
})

ECS_CTOR(vkm_quat, ptr, {
  *ptr = CVKM_QUAT_IDENTITY;
})

ECS_CTOR(vkm_versor, ptr, {
  *ptr = CVKM_QUAT_IDENTITY;
})

ECS_CTOR(Rotation3D, ptr, {
  *ptr = CVKM_QUAT_IDENTITY;
})

ECS_CTOR(Scale, ptr, {
  *ptr = 1.0f;
})

ECS_CTOR(Scale2D, ptr, {
  *ptr = (Scale2D){ { 1.0f, 1.0f } };
})

ECS_CTOR(Scale3D, ptr, {
  *ptr = (Scale3D){ { 1.0f, 1.0f, 1.0f } };
})

ECS_CTOR(Scale4D, ptr, {
  *ptr = (Scale4D){ { 1.0f, 1.0f, 1.0f, 1.0f } };
})

ECS_CTOR(Transform, ptr, {
  *ptr = CVKM_MAT4_IDENTITY;
})

ECS_CTOR(Damping, ptr, {
  *ptr = 0.999f;
})

ECS_CTOR(Gravity2D, ptr, {
  *ptr = (vkm_vec2){ { 0.0f, -9.80665f } };
})

ECS_CTOR(Gravity3D, ptr, {
  *ptr = (vkm_vec3){ { 0.0f, -9.80665f, 0.0f } };
})

ECS_CTOR(Gravity4D, ptr, {
  *ptr = (vkm_vec4){ { 0.0f, -9.80665f, 0.0f, 0.0f } };
})

#define CVKM_MEMBER(member, member_type, struct_type, unit_) {\
  .name = #member,\
  .type = ecs_id(member_type),\
  .offset = offsetof(struct_type, member),\
  .unit = unit_,\
}

#define CVKM_VEC2_COMPONENT(name, struct_type, member_type, unit_) ECS_COMPONENT_DEFINE(world, struct_type);\
  ecs_struct(\
    world,\
    {\
      .entity = ecs_id(struct_type),\
      .members = {\
        CVKM_MEMBER(x, member_type, struct_type, unit_),\
        CVKM_MEMBER(y, member_type, struct_type, unit_),\
      },\
    }\
  );\
  ecs_set_name(world, ecs_id(struct_type), #name);\
  ecs_set_symbol(world, ecs_id(struct_type), #name)

#define CVKM_VEC3_COMPONENT(name, struct_type, member_type, unit_) ECS_COMPONENT_DEFINE(world, struct_type);\
  ecs_struct(\
    world,\
    {\
      .entity = ecs_id(struct_type),\
      .members = {\
        CVKM_MEMBER(x, member_type, struct_type, unit_),\
        CVKM_MEMBER(y, member_type, struct_type, unit_),\
        CVKM_MEMBER(z, member_type, struct_type, unit_),\
      },\
    }\
  );\
  ecs_set_name(world, ecs_id(struct_type), #name);\
  ecs_set_symbol(world, ecs_id(struct_type), #name)

#define CVKM_VEC4_COMPONENT(name, struct_type, member_type, unit_) ECS_COMPONENT_DEFINE(world, struct_type);\
  ecs_struct(\
    world,\
    {\
      .entity = ecs_id(struct_type),\
      .members = {\
        CVKM_MEMBER(x, member_type, struct_type, unit_),\
        CVKM_MEMBER(y, member_type, struct_type, unit_),\
        CVKM_MEMBER(z, member_type, struct_type, unit_),\
        CVKM_MEMBER(w, member_type, struct_type, unit_),\
      },\
    }\
  );\
  ecs_set_name(world, ecs_id(struct_type), #name);\
  ecs_set_symbol(world, ecs_id(struct_type), #name)

#define CVKM_MAT4_COMPONENT(name, struct_type, member_type) ECS_COMPONENT_DEFINE(world, struct_type);\
  ecs_struct(\
    world,\
    {\
      .entity = ecs_id(struct_type),\
      .members = {\
        CVKM_MEMBER(m00, member_type, struct_type, 0),\
        CVKM_MEMBER(m01, member_type, struct_type, 0),\
        CVKM_MEMBER(m02, member_type, struct_type, 0),\
        CVKM_MEMBER(m03, member_type, struct_type, 0),\
        CVKM_MEMBER(m10, member_type, struct_type, 0),\
        CVKM_MEMBER(m11, member_type, struct_type, 0),\
        CVKM_MEMBER(m12, member_type, struct_type, 0),\
        CVKM_MEMBER(m13, member_type, struct_type, 0),\
        CVKM_MEMBER(m20, member_type, struct_type, 0),\
        CVKM_MEMBER(m21, member_type, struct_type, 0),\
        CVKM_MEMBER(m22, member_type, struct_type, 0),\
        CVKM_MEMBER(m23, member_type, struct_type, 0),\
        CVKM_MEMBER(m30, member_type, struct_type, 0),\
        CVKM_MEMBER(m31, member_type, struct_type, 0),\
        CVKM_MEMBER(m32, member_type, struct_type, 0),\
        CVKM_MEMBER(m33, member_type, struct_type, 0),\
      },\
    }\
  );\
  ecs_set_name(world, ecs_id(struct_type), #name);\
  ecs_set_symbol(world, ecs_id(struct_type), #name)

void cvkmImport(ecs_world_t* world) {
  ECS_MODULE(world, cvkm);

  ECS_IMPORT(world, FlecsUnits);

  CVKM_VEC2_COMPONENT(bvec2, vkm_bvec2, ecs_i8_t, 0);
  CVKM_VEC2_COMPONENT(ubvec2, vkm_ubvec2, ecs_u8_t, 0);
  CVKM_VEC2_COMPONENT(svec2, vkm_svec2, ecs_i16_t, 0);
  CVKM_VEC2_COMPONENT(usvec2, vkm_usvec2, ecs_u16_t, 0);
  CVKM_VEC2_COMPONENT(ivec2, vkm_ivec2, ecs_i32_t, 0);
  CVKM_VEC2_COMPONENT(uvec2, vkm_uvec2, ecs_u32_t, 0);
  CVKM_VEC2_COMPONENT(lvec2, vkm_lvec2, ecs_i64_t, 0);
  CVKM_VEC2_COMPONENT(ulvec2, vkm_ulvec2, ecs_u64_t, 0);
  CVKM_VEC2_COMPONENT(vec2, vkm_vec2, ecs_f32_t, 0);
  CVKM_VEC2_COMPONENT(dvec2, vkm_dvec2, ecs_f64_t, 0);
  CVKM_VEC3_COMPONENT(bvec3, vkm_bvec3, ecs_i8_t, 0);
  CVKM_VEC3_COMPONENT(ubvec3, vkm_ubvec3, ecs_u8_t, 0);
  CVKM_VEC3_COMPONENT(svec3, vkm_svec3, ecs_i16_t, 0);
  CVKM_VEC3_COMPONENT(usvec3, vkm_usvec3, ecs_u16_t, 0);
  CVKM_VEC3_COMPONENT(ivec3, vkm_ivec3, ecs_i32_t, 0);
  CVKM_VEC3_COMPONENT(uvec3, vkm_uvec3, ecs_u32_t, 0);
  CVKM_VEC3_COMPONENT(lvec3, vkm_lvec3, ecs_i64_t, 0);
  CVKM_VEC3_COMPONENT(ulvec3, vkm_ulvec3, ecs_u64_t, 0);
  CVKM_VEC3_COMPONENT(vec3, vkm_vec3, ecs_f32_t, 0);
  CVKM_VEC3_COMPONENT(dvec3, vkm_dvec3, ecs_f64_t, 0);
  CVKM_VEC4_COMPONENT(bvec4, vkm_bvec4, ecs_i8_t, 0);
  CVKM_VEC4_COMPONENT(ubvec4, vkm_ubvec4, ecs_u8_t, 0);
  CVKM_VEC4_COMPONENT(svec4, vkm_svec4, ecs_i16_t, 0);
  CVKM_VEC4_COMPONENT(usvec4, vkm_usvec4, ecs_u16_t, 0);
  CVKM_VEC4_COMPONENT(ivec4, vkm_ivec4, ecs_i32_t, 0);
  CVKM_VEC4_COMPONENT(uvec4, vkm_uvec4, ecs_u32_t, 0);
  CVKM_VEC4_COMPONENT(lvec4, vkm_lvec4, ecs_i64_t, 0);
  CVKM_VEC4_COMPONENT(ulvec4, vkm_ulvec4, ecs_u64_t, 0);
  CVKM_VEC4_COMPONENT(vec4, vkm_vec4, ecs_f32_t, 0);
  CVKM_VEC4_COMPONENT(dvec4, vkm_dvec4, ecs_f64_t, 0);
  ECS_COMPONENT_DEFINE(world, vkm_mat3);
  ecs_struct(
    world,
    {
      .entity = ecs_id(vkm_mat3),
      .members = {
        CVKM_MEMBER(m00, ecs_f32_t, vkm_mat3, 0),
        CVKM_MEMBER(m01, ecs_f32_t, vkm_mat3, 0),
        CVKM_MEMBER(m02, ecs_f32_t, vkm_mat3, 0),
        CVKM_MEMBER(m10, ecs_f32_t, vkm_mat3, 0),
        CVKM_MEMBER(m11, ecs_f32_t, vkm_mat3, 0),
        CVKM_MEMBER(m12, ecs_f32_t, vkm_mat3, 0),
        CVKM_MEMBER(m20, ecs_f32_t, vkm_mat3, 0),
        CVKM_MEMBER(m21, ecs_f32_t, vkm_mat3, 0),
        CVKM_MEMBER(m22, ecs_f32_t, vkm_mat3, 0),
      },
    }
  );
  ecs_set_name(world, ecs_id(vkm_mat3), "mat3");
  ecs_set_symbol(world, ecs_id(vkm_mat3), "mat3");
  CVKM_MAT4_COMPONENT(mat4, vkm_mat4, ecs_f32_t);
  CVKM_VEC4_COMPONENT(quat, vkm_quat, ecs_f32_t, 0);
  ECS_COMPONENT_DEFINE(world, vkm_versor);
  ecs_add_pair(world, ecs_id(vkm_versor), EcsIsA, ecs_id(vkm_quat));
  CVKM_VEC2_COMPONENT(Position2D, Position2D, ecs_f32_t, EcsMeters);
  CVKM_VEC3_COMPONENT(Position3D, Position3D, ecs_f32_t, EcsMeters);
  CVKM_VEC4_COMPONENT(Position4D, Position4D, ecs_f32_t, EcsMeters);
  CVKM_VEC2_COMPONENT(DoublePosition2D, DoublePosition2D, ecs_f64_t, EcsMeters);
  CVKM_VEC3_COMPONENT(DoublePosition3D, DoublePosition3D, ecs_f64_t, EcsMeters);
  CVKM_VEC4_COMPONENT(DoublePosition4D, DoublePosition4D, ecs_f64_t, EcsMeters);
  ECS_COMPONENT_DEFINE(world, Rotation2D);
  ecs_add_pair(world, ecs_id(Rotation2D), EcsIsA, EcsRadians);
  ECS_COMPONENT_DEFINE(world, Rotation3D);
  ecs_add_pair(world, ecs_id(Rotation3D), EcsIsA, ecs_id(vkm_versor));
  ECS_COMPONENT_DEFINE(world, Scale);
  ecs_add_pair(world, ecs_id(Scale), EcsIsA, EcsF32);
  ECS_COMPONENT_DEFINE(world, Scale2D);
  ecs_add_pair(world, ecs_id(Scale2D), EcsIsA, ecs_id(vkm_vec2));
  ECS_COMPONENT_DEFINE(world, Scale3D);
  ecs_add_pair(world, ecs_id(Scale3D), EcsIsA, ecs_id(vkm_vec3));
  ECS_COMPONENT_DEFINE(world, Scale4D);
  ecs_add_pair(world, ecs_id(Scale4D), EcsIsA, ecs_id(vkm_vec4));
  CVKM_MAT4_COMPONENT(Transform, Transform, ecs_f32_t);
  CVKM_VEC2_COMPONENT(Velocity2D, Velocity2D, ecs_f32_t, EcsMetersPerSecond);
  CVKM_VEC3_COMPONENT(Velocity3D, Velocity3D, ecs_f32_t, EcsMetersPerSecond);
  CVKM_VEC4_COMPONENT(Velocity4D, Velocity4D, ecs_f32_t, EcsMetersPerSecond);
  CVKM_VEC2_COMPONENT(Acceleration2D, Acceleration2D, ecs_f32_t, 0);
  CVKM_VEC3_COMPONENT(Acceleration3D, Acceleration3D, ecs_f32_t, 0);
  CVKM_VEC4_COMPONENT(Acceleration4D, Acceleration4D, ecs_f32_t, 0);
  CVKM_VEC2_COMPONENT(Force2D, Force2D, ecs_f32_t, EcsNewton);
  CVKM_VEC3_COMPONENT(Force3D, Force3D, ecs_f32_t, EcsNewton);
  CVKM_VEC4_COMPONENT(Force4D, Force4D, ecs_f32_t, EcsNewton);
  ECS_COMPONENT_DEFINE(world, Mass);
  ecs_add_pair(world, ecs_id(Mass), EcsIsA, EcsKiloGrams);
  ECS_COMPONENT_DEFINE(world, Damping);
  ecs_primitive(world, { .entity = ecs_id(Damping), .kind = EcsF32 });
  ECS_COMPONENT_DEFINE(world, Gravity2D);
  ecs_add_pair(world, ecs_id(Gravity2D), EcsIsA, ecs_id(Acceleration2D));
  ECS_COMPONENT_DEFINE(world, Gravity3D);
  ecs_add_pair(world, ecs_id(Gravity3D), EcsIsA, ecs_id(Acceleration3D));
  ECS_COMPONENT_DEFINE(world, Gravity4D);
  ecs_add_pair(world, ecs_id(Gravity4D), EcsIsA, ecs_id(Acceleration4D));
  ECS_COMPONENT_DEFINE(world, GravityScale);
  ecs_primitive(world, { .entity = ecs_id(GravityScale), .kind = EcsF32 });

  ecs_set_hooks(world, vkm_bvec2, { .ctor = ecs_ctor(vkm_bvec2) });
  ecs_set_hooks(world, vkm_ubvec2, { .ctor = ecs_ctor(vkm_ubvec2) });
  ecs_set_hooks(world, vkm_svec2, { .ctor = ecs_ctor(vkm_svec2) });
  ecs_set_hooks(world, vkm_usvec2, { .ctor = ecs_ctor(vkm_usvec2) });
  ecs_set_hooks(world, vkm_ivec2, { .ctor = ecs_ctor(vkm_ivec2) });
  ecs_set_hooks(world, vkm_uvec2, { .ctor = ecs_ctor(vkm_uvec2) });
  ecs_set_hooks(world, vkm_lvec2, { .ctor = ecs_ctor(vkm_lvec2) });
  ecs_set_hooks(world, vkm_ulvec2, { .ctor = ecs_ctor(vkm_ulvec2) });
  ecs_set_hooks(world, vkm_vec2, { .ctor = ecs_ctor(vkm_vec2) });
  ecs_set_hooks(world, vkm_dvec2, { .ctor = ecs_ctor(vkm_dvec2) });
  ecs_set_hooks(world, vkm_bvec3, { .ctor = ecs_ctor(vkm_bvec3) });
  ecs_set_hooks(world, vkm_ubvec3, { .ctor = ecs_ctor(vkm_ubvec3) });
  ecs_set_hooks(world, vkm_svec3, { .ctor = ecs_ctor(vkm_svec3) });
  ecs_set_hooks(world, vkm_usvec3, { .ctor = ecs_ctor(vkm_usvec3) });
  ecs_set_hooks(world, vkm_ivec3, { .ctor = ecs_ctor(vkm_ivec3) });
  ecs_set_hooks(world, vkm_uvec3, { .ctor = ecs_ctor(vkm_uvec3) });
  ecs_set_hooks(world, vkm_lvec3, { .ctor = ecs_ctor(vkm_lvec3) });
  ecs_set_hooks(world, vkm_ulvec3, { .ctor = ecs_ctor(vkm_ulvec3) });
  ecs_set_hooks(world, vkm_vec3, { .ctor = ecs_ctor(vkm_vec3) });
  ecs_set_hooks(world, vkm_dvec3, { .ctor = ecs_ctor(vkm_dvec3) });
  ecs_set_hooks(world, vkm_bvec4, { .ctor = ecs_ctor(vkm_bvec4) });
  ecs_set_hooks(world, vkm_ubvec4, { .ctor = ecs_ctor(vkm_ubvec4) });
  ecs_set_hooks(world, vkm_svec4, { .ctor = ecs_ctor(vkm_svec4) });
  ecs_set_hooks(world, vkm_usvec4, { .ctor = ecs_ctor(vkm_usvec4) });
  ecs_set_hooks(world, vkm_ivec4, { .ctor = ecs_ctor(vkm_ivec4) });
  ecs_set_hooks(world, vkm_uvec4, { .ctor = ecs_ctor(vkm_uvec4) });
  ecs_set_hooks(world, vkm_lvec4, { .ctor = ecs_ctor(vkm_lvec4) });
  ecs_set_hooks(world, vkm_ulvec4, { .ctor = ecs_ctor(vkm_ulvec4) });
  ecs_set_hooks(world, vkm_vec4, { .ctor = ecs_ctor(vkm_vec4) });
  ecs_set_hooks(world, vkm_dvec4, { .ctor = ecs_ctor(vkm_dvec4) });
  ecs_set_hooks(world, vkm_mat3, { .ctor = ecs_ctor(vkm_mat3) });
  ecs_set_hooks(world, vkm_mat4, { .ctor = ecs_ctor(vkm_mat4) });
  ecs_set_hooks(world, vkm_quat, { .ctor = ecs_ctor(vkm_quat) });
  ecs_set_hooks(world, vkm_versor, { .ctor = ecs_ctor(vkm_versor) });
  ecs_set_hooks(world, Position2D, { .ctor = ecs_ctor(Position2D) });
  ecs_set_hooks(world, Position3D, { .ctor = ecs_ctor(Position3D) });
  ecs_set_hooks(world, Position4D, { .ctor = ecs_ctor(Position4D) });
  ecs_set_hooks(world, DoublePosition2D, { .ctor = ecs_ctor(DoublePosition2D) });
  ecs_set_hooks(world, DoublePosition3D, { .ctor = ecs_ctor(DoublePosition3D) });
  ecs_set_hooks(world, DoublePosition4D, { .ctor = ecs_ctor(DoublePosition4D) });
  ecs_set_hooks(world, Rotation2D, { .ctor = ecs_ctor(Rotation2D) });
  ecs_set_hooks(world, Rotation3D, { .ctor = ecs_ctor(Rotation3D) });
  ecs_set_hooks(world, Scale, { .ctor = ecs_ctor(Scale) });
  ecs_set_hooks(world, Scale2D, { .ctor = ecs_ctor(Scale2D) });
  ecs_set_hooks(world, Scale3D, { .ctor = ecs_ctor(Scale3D) });
  ecs_set_hooks(world, Scale4D, { .ctor = ecs_ctor(Scale4D) });
  ecs_set_hooks(world, Transform, { .ctor = ecs_ctor(Transform) });
  ecs_set_hooks(world, Velocity2D, { .ctor = ecs_ctor(Velocity2D) });
  ecs_set_hooks(world, Velocity3D, { .ctor = ecs_ctor(Velocity3D) });
  ecs_set_hooks(world, Velocity4D, { .ctor = ecs_ctor(Velocity4D) });
  ecs_set_hooks(world, Acceleration2D, { .ctor = ecs_ctor(Acceleration2D) });
  ecs_set_hooks(world, Acceleration3D, { .ctor = ecs_ctor(Acceleration3D) });
  ecs_set_hooks(world, Acceleration4D, { .ctor = ecs_ctor(Acceleration4D) });
  ecs_set_hooks(world, Force2D, { .ctor = ecs_ctor(Force2D) });
  ecs_set_hooks(world, Force3D, { .ctor = ecs_ctor(Force3D) });
  ecs_set_hooks(world, Force4D, { .ctor = ecs_ctor(Force4D) });
  ecs_set_hooks(world, Mass, { .ctor = ecs_ctor(Mass) });
  ecs_set_hooks(world, Damping, { .ctor = ecs_ctor(Damping) });
  ecs_set_hooks(world, Gravity2D, { .ctor = ecs_ctor(Gravity2D) });
  ecs_set_hooks(world, Gravity3D, { .ctor = ecs_ctor(Gravity3D) });
  ecs_set_hooks(world, Gravity4D, { .ctor = ecs_ctor(Gravity4D) });
  ecs_set_hooks(world, GravityScale, { .ctor = ecs_ctor(GravityScale) });
}

#undef CVKM_FLECS_IMPLEMENTATION
#endif
#endif

#if !defined(_MSC_VER) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif
