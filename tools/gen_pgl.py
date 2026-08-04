#!/usr/bin/env python3
"""Generate include/pgl.hpp -- the GLSL-compatible vector math header.

Swizzles are generated rather than hand-written so that *every* GLSL swizzle
exists. That matters because shaders are often machine-generated (see
`ppm-gen`), and a missing `.zwxy` would be a compile error the generator
cannot see.

Usage: python3 tools/gen_pgl.py > include/pgl.hpp
"""

from itertools import product

# --------------------------------------------------------------------------
# swizzle generation
# --------------------------------------------------------------------------

FIELDS = {"vec2": "xy", "vec3": "xyz", "vec4": "xyzw"}
RET = {1: "float", 2: "vec2", 3: "vec3", 4: "vec4"}

# Curated rgba aliases. Full rgba coverage would double the header for little
# gain; these are the forms that actually show up in shader code.
RGBA_ALIASES = [
    "rgb", "rgba", "rg", "gb", "ba", "bgr", "bgra", "argb", "abgr",
    "rrr", "ggg", "bbb", "aaa", "rrrr", "gggg", "bbbb", "aaaa", "rgbr",
]
RGBA_TO_XYZW = str.maketrans("rgba", "xyzw")


def swizzles_for(typename):
    """Yield (name, return_type, body_args) for every swizzle of `typename`."""
    src = FIELDS[typename]
    own_len = len(src)
    for n in (2, 3, 4):
        for combo in product(src, repeat=n):
            yield "".join(combo), RET[n], list(combo)
    # rgba aliases, only where every referenced component exists
    for alias in RGBA_ALIASES:
        xyzw = alias.translate(RGBA_TO_XYZW)
        if any(c not in src for c in xyzw):
            continue
        if len(alias) < 2:
            continue
        yield alias, RET[len(alias)], list(xyzw)
    # single-component rgba accessors
    for i, alias in enumerate("rgba"[:own_len]):
        yield alias, "float", [src[i]]


def emit_swizzles(typename, complete):
    """Emit swizzle methods for `typename`.

    `complete` is the set of types already fully defined at this point in the
    header. A method whose return type is not yet complete is emitted as a
    template with a defaulted type parameter: the body is only checked at
    instantiation, by which time the type is complete. Call syntax is
    unchanged (`v.xyyx()`).
    """
    out = []
    for name, ret, args in sorted(set((n, r, tuple(a)) for n, r, a in swizzles_for(typename))):
        args = list(args)
        ctor_args = ", ".join(args)
        if ret == "float":
            out.append(f"    float {name}() const {{ return {args[0]}; }}")
        elif ret in complete or ret == typename:
            # `typename` itself is usable inside its own member function bodies
            out.append(f"    {ret} {name}() const {{ return {ret}({ctor_args}); }}")
        else:
            out.append(
                f"    template<class V = {ret}> V {name}() const "
                f"{{ return V({ctor_args}); }}"
            )
    return "\n".join(out)


# --------------------------------------------------------------------------
# hand-written core
# --------------------------------------------------------------------------

HEAD = r'''// ===========================================================================
//  pgl.hpp -- GLSL-compatible scalar/vector/matrix math for CPU shaders.
//
//  GENERATED FILE. Edit tools/gen_pgl.py and re-run:
//      python3 tools/gen_pgl.py > include/pgl.hpp
//
//  Differences from GLSL, and the only edits needed when porting a shader:
//    1. Swizzles are methods, so they need parentheses: v.xyyx  ->  v.xyyx()
//    2. Float literals need no `.` suffix but keep them for readability.
//    3. There is no dFdx/dFdy/fwidth here. Screen-space derivatives do not
//       exist for a scalar CPU kernel; see aa.hpp for the CPU equivalents.
//
//  Everything else -- component-wise builtins, operator overloads, matrix
//  column-major semantics -- matches GLSL ES 3.0 behaviour.
// ===========================================================================
#ifndef PGL_HPP
#define PGL_HPP

#include <cmath>

namespace pgl {

// Forward declarations: swizzles on a smaller vector need to name a larger one.
struct vec2;
struct vec3;
struct vec4;

'''

VEC2_CORE = r'''struct vec2 {
    float x, y;

    vec2() : x(0), y(0) {}
    explicit vec2(float s) : x(s), y(s) {}
    vec2(float x_, float y_) : x(x_), y(y_) {}

    float  operator[](int i) const { return i == 0 ? x : y; }
    float &operator[](int i)       { return i == 0 ? x : y; }

'''

VEC3_CORE = r'''struct vec3 {
    float x, y, z;

    vec3() : x(0), y(0), z(0) {}
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    vec3(const vec2 &a, float z_) : x(a.x), y(a.y), z(z_) {}
    vec3(float x_, const vec2 &a) : x(x_), y(a.x), z(a.y) {}

    float  operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float &operator[](int i)       { return i == 0 ? x : (i == 1 ? y : z); }

'''

VEC4_CORE = r'''struct vec4 {
    float x, y, z, w;

    vec4() : x(0), y(0), z(0), w(0) {}
    explicit vec4(float s) : x(s), y(s), z(s), w(s) {}
    vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    vec4(const vec2 &a, const vec2 &b) : x(a.x), y(a.y), z(b.x), w(b.y) {}
    vec4(const vec2 &a, float z_, float w_) : x(a.x), y(a.y), z(z_), w(w_) {}
    vec4(float x_, const vec2 &a, float w_) : x(x_), y(a.x), z(a.y), w(w_) {}
    vec4(float x_, float y_, const vec2 &a) : x(x_), y(y_), z(a.x), w(a.y) {}
    vec4(const vec3 &a, float w_) : x(a.x), y(a.y), z(a.z), w(w_) {}
    vec4(float x_, const vec3 &a) : x(x_), y(a.x), z(a.y), w(a.z) {}

    float  operator[](int i) const { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    float &operator[](int i)       { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }

'''

OPS = r'''
// ---------------------------------------------------------------------------
// operators
// ---------------------------------------------------------------------------

#define PGL_VEC_OPS2(V, EXPR_ADD, EXPR_SUB, EXPR_MUL, EXPR_DIV, EXPR_NEG,  \
                     SCL_ADD, SCL_SUB, SCL_MUL, SCL_DIV,                   \
                     LCS_SUB, LCS_DIV)                                     \
    inline V operator+(const V &a, const V &b) { return EXPR_ADD; }        \
    inline V operator-(const V &a, const V &b) { return EXPR_SUB; }        \
    inline V operator*(const V &a, const V &b) { return EXPR_MUL; }        \
    inline V operator/(const V &a, const V &b) { return EXPR_DIV; }        \
    inline V operator-(const V &a)             { return EXPR_NEG; }        \
    inline V operator+(const V &a, float s)    { return SCL_ADD; }         \
    inline V operator-(const V &a, float s)    { return SCL_SUB; }         \
    inline V operator*(const V &a, float s)    { return SCL_MUL; }         \
    inline V operator/(const V &a, float s)    { return SCL_DIV; }         \
    inline V operator+(float s, const V &a)    { return SCL_ADD; }         \
    inline V operator*(float s, const V &a)    { return SCL_MUL; }         \
    inline V operator-(float s, const V &a)    { return LCS_SUB; }         \
    inline V operator/(float s, const V &a)    { return LCS_DIV; }         \
    inline V &operator+=(V &a, const V &b) { a = a + b; return a; }        \
    inline V &operator-=(V &a, const V &b) { a = a - b; return a; }        \
    inline V &operator*=(V &a, const V &b) { a = a * b; return a; }        \
    inline V &operator/=(V &a, const V &b) { a = a / b; return a; }        \
    inline V &operator+=(V &a, float s)    { a = a + s; return a; }        \
    inline V &operator-=(V &a, float s)    { a = a - s; return a; }        \
    inline V &operator*=(V &a, float s)    { a = a * s; return a; }        \
    inline V &operator/=(V &a, float s)    { a = a / s; return a; }

PGL_VEC_OPS2(vec2,
    vec2(a.x + b.x, a.y + b.y), vec2(a.x - b.x, a.y - b.y),
    vec2(a.x * b.x, a.y * b.y), vec2(a.x / b.x, a.y / b.y),
    vec2(-a.x, -a.y),
    vec2(a.x + s, a.y + s), vec2(a.x - s, a.y - s),
    vec2(a.x * s, a.y * s), vec2(a.x / s, a.y / s),
    vec2(s - a.x, s - a.y), vec2(s / a.x, s / a.y))

PGL_VEC_OPS2(vec3,
    vec3(a.x + b.x, a.y + b.y, a.z + b.z), vec3(a.x - b.x, a.y - b.y, a.z - b.z),
    vec3(a.x * b.x, a.y * b.y, a.z * b.z), vec3(a.x / b.x, a.y / b.y, a.z / b.z),
    vec3(-a.x, -a.y, -a.z),
    vec3(a.x + s, a.y + s, a.z + s), vec3(a.x - s, a.y - s, a.z - s),
    vec3(a.x * s, a.y * s, a.z * s), vec3(a.x / s, a.y / s, a.z / s),
    vec3(s - a.x, s - a.y, s - a.z), vec3(s / a.x, s / a.y, s / a.z))

PGL_VEC_OPS2(vec4,
    vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w),
    vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w),
    vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w),
    vec4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w),
    vec4(-a.x, -a.y, -a.z, -a.w),
    vec4(a.x + s, a.y + s, a.z + s, a.w + s),
    vec4(a.x - s, a.y - s, a.z - s, a.w - s),
    vec4(a.x * s, a.y * s, a.z * s, a.w * s),
    vec4(a.x / s, a.y / s, a.z / s, a.w / s),
    vec4(s - a.x, s - a.y, s - a.z, s - a.w),
    vec4(s / a.x, s / a.y, s / a.z, s / a.w))

#undef PGL_VEC_OPS2

// ---------------------------------------------------------------------------
// component-wise builtins
// ---------------------------------------------------------------------------

// IMPORTANT -- why some functions below have no `float` overload.
//
// C++ already declares float overloads of the <cmath> functions (abs, sin,
// sqrt, pow, ...) in the global namespace. Shader files do `using namespace
// pgl`, so if pgl ALSO declared `float abs(float)`, then an unqualified
// `abs(0.5f)` would see two equally good candidates and fail to compile with
// "call to 'abs' is ambiguous". That is a miserable error to hit in generated
// shader code, so for those names pgl defines only the vector overloads and
// lets scalar calls fall through to the standard library. The behaviour is
// identical either way -- ::abs(float) is fabsf.
//
// Functions GLSL has but C does not (fract, sign, mix, clamp, mod, step, ...)
// do get float overloads here, because nothing collides with them.

/// Vector-only: applies the f-suffixed C function per component.
#define PGL_UNARY_STD(NAME, CFN)                                               \
    inline vec2 NAME(const vec2 &v) { return vec2(CFN(v.x), CFN(v.y)); }       \
    inline vec3 NAME(const vec3 &v) { return vec3(CFN(v.x), CFN(v.y), CFN(v.z)); } \
    inline vec4 NAME(const vec4 &v) { return vec4(CFN(v.x), CFN(v.y), CFN(v.z), CFN(v.w)); }

PGL_UNARY_STD(sin,   sinf)
PGL_UNARY_STD(cos,   cosf)
PGL_UNARY_STD(tan,   tanf)
PGL_UNARY_STD(asin,  asinf)
PGL_UNARY_STD(acos,  acosf)
PGL_UNARY_STD(sinh,  sinhf)
PGL_UNARY_STD(cosh,  coshf)
PGL_UNARY_STD(tanh,  tanhf)
PGL_UNARY_STD(exp,   expf)
PGL_UNARY_STD(log,   logf)
PGL_UNARY_STD(exp2,  exp2f)
PGL_UNARY_STD(log2,  log2f)
PGL_UNARY_STD(sqrt,  sqrtf)
PGL_UNARY_STD(abs,   fabsf)
PGL_UNARY_STD(floor, floorf)
PGL_UNARY_STD(ceil,  ceilf)
PGL_UNARY_STD(trunc, truncf)
PGL_UNARY_STD(round, roundf)

#undef PGL_UNARY_STD

/// Float + vector, for the GLSL-only functions with no <cmath> counterpart.
#define PGL_UNARY(NAME, BODY)                                                  \
    inline float NAME(float a) { return BODY; }                                \
    inline vec2 NAME(const vec2 &v) { return vec2(NAME(v.x), NAME(v.y)); }     \
    inline vec3 NAME(const vec3 &v) { return vec3(NAME(v.x), NAME(v.y), NAME(v.z)); } \
    inline vec4 NAME(const vec4 &v) { return vec4(NAME(v.x), NAME(v.y), NAME(v.z), NAME(v.w)); }

PGL_UNARY(inversesqrt,  1.0f / sqrtf(a))
PGL_UNARY(fract,        a - floorf(a))
PGL_UNARY(sign,         (float)((a > 0.0f) - (a < 0.0f)))
PGL_UNARY(radians,      a * 0.01745329251994329577f)
PGL_UNARY(degrees,      a * 57.29577951308232088f)

#undef PGL_UNARY

// Two arguments. Provides (vec,vec), (vec,float) and (float,vec) forms so that
// GLSL's implicit scalar broadcasting keeps working.
#define PGL_BINARY(NAME, BODY)                                                 \
    inline float NAME(float a, float b) { return BODY; }                       \
    inline vec2 NAME(const vec2 &a, const vec2 &b) { return vec2(NAME(a.x, b.x), NAME(a.y, b.y)); } \
    inline vec3 NAME(const vec3 &a, const vec3 &b) { return vec3(NAME(a.x, b.x), NAME(a.y, b.y), NAME(a.z, b.z)); } \
    inline vec4 NAME(const vec4 &a, const vec4 &b) { return vec4(NAME(a.x, b.x), NAME(a.y, b.y), NAME(a.z, b.z), NAME(a.w, b.w)); } \
    inline vec2 NAME(const vec2 &a, float b) { return NAME(a, vec2(b)); }      \
    inline vec3 NAME(const vec3 &a, float b) { return NAME(a, vec3(b)); }      \
    inline vec4 NAME(const vec4 &a, float b) { return NAME(a, vec4(b)); }      \
    inline vec2 NAME(float a, const vec2 &b) { return NAME(vec2(a), b); }      \
    inline vec3 NAME(float a, const vec3 &b) { return NAME(vec3(a), b); }      \
    inline vec4 NAME(float a, const vec4 &b) { return NAME(vec4(a), b); }

PGL_BINARY(min,  fminf(a, b))
PGL_BINARY(max,  fmaxf(a, b))
PGL_BINARY(step, a > b ? 0.0f : 1.0f)   // GLSL: step(edge, x) -> x < edge ? 0 : 1
PGL_BINARY(atan, atan2f(a, b))          // GLSL: atan(y, x). No 2-arg ::atan, so safe.
// GLSL mod() uses floor, not truncation, so it stays positive for negative inputs.
// This differs from fmod() and is a common source of seams in tiled patterns.
PGL_BINARY(mod,  a - b * floorf(a / b))

#undef PGL_BINARY

/// Vector-only two-argument form; see the note above PGL_UNARY_STD.
#define PGL_BINARY_STD(NAME, CFN)                                              \
    inline vec2 NAME(const vec2 &a, const vec2 &b) { return vec2(CFN(a.x, b.x), CFN(a.y, b.y)); } \
    inline vec3 NAME(const vec3 &a, const vec3 &b) { return vec3(CFN(a.x, b.x), CFN(a.y, b.y), CFN(a.z, b.z)); } \
    inline vec4 NAME(const vec4 &a, const vec4 &b) { return vec4(CFN(a.x, b.x), CFN(a.y, b.y), CFN(a.z, b.z), CFN(a.w, b.w)); } \
    inline vec2 NAME(const vec2 &a, float b) { return NAME(a, vec2(b)); }      \
    inline vec3 NAME(const vec3 &a, float b) { return NAME(a, vec3(b)); }      \
    inline vec4 NAME(const vec4 &a, float b) { return NAME(a, vec4(b)); }      \
    inline vec2 NAME(float a, const vec2 &b) { return NAME(vec2(a), b); }      \
    inline vec3 NAME(float a, const vec3 &b) { return NAME(vec3(a), b); }      \
    inline vec4 NAME(float a, const vec4 &b) { return NAME(vec4(a), b); }

PGL_BINARY_STD(pow, powf)

#undef PGL_BINARY_STD

// Single-argument atan on vectors. The float form is ::atan from <cmath>.
inline vec2 atan(const vec2 &v) { return vec2(atanf(v.x), atanf(v.y)); }
inline vec3 atan(const vec3 &v) { return vec3(atanf(v.x), atanf(v.y), atanf(v.z)); }
inline vec4 atan(const vec4 &v) { return vec4(atanf(v.x), atanf(v.y), atanf(v.z), atanf(v.w)); }

// Three arguments.
#define PGL_TERNARY(NAME, BODY)                                                \
    inline float NAME(float a, float b, float c) { return BODY; }              \
    inline vec2 NAME(const vec2 &a, const vec2 &b, const vec2 &c) { return vec2(NAME(a.x, b.x, c.x), NAME(a.y, b.y, c.y)); } \
    inline vec3 NAME(const vec3 &a, const vec3 &b, const vec3 &c) { return vec3(NAME(a.x, b.x, c.x), NAME(a.y, b.y, c.y), NAME(a.z, b.z, c.z)); } \
    inline vec4 NAME(const vec4 &a, const vec4 &b, const vec4 &c) { return vec4(NAME(a.x, b.x, c.x), NAME(a.y, b.y, c.y), NAME(a.z, b.z, c.z), NAME(a.w, b.w, c.w)); } \
    inline vec2 NAME(const vec2 &a, float b, float c) { return NAME(a, vec2(b), vec2(c)); } \
    inline vec3 NAME(const vec3 &a, float b, float c) { return NAME(a, vec3(b), vec3(c)); } \
    inline vec4 NAME(const vec4 &a, float b, float c) { return NAME(a, vec4(b), vec4(c)); }

PGL_TERNARY(clamp, fminf(fmaxf(a, b), c))
PGL_TERNARY(mix,   a * (1.0f - c) + b * c)

#undef PGL_TERNARY

// smoothstep needs its own definition because the clamp is on the third arg.
inline float smoothstep(float e0, float e1, float x) {
    float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline vec2 smoothstep(const vec2 &e0, const vec2 &e1, const vec2 &x) {
    return vec2(smoothstep(e0.x, e1.x, x.x), smoothstep(e0.y, e1.y, x.y));
}
inline vec3 smoothstep(const vec3 &e0, const vec3 &e1, const vec3 &x) {
    return vec3(smoothstep(e0.x, e1.x, x.x), smoothstep(e0.y, e1.y, x.y), smoothstep(e0.z, e1.z, x.z));
}
inline vec4 smoothstep(const vec4 &e0, const vec4 &e1, const vec4 &x) {
    return vec4(smoothstep(e0.x, e1.x, x.x), smoothstep(e0.y, e1.y, x.y), smoothstep(e0.z, e1.z, x.z), smoothstep(e0.w, e1.w, x.w));
}
inline vec2 smoothstep(float e0, float e1, const vec2 &x) { return smoothstep(vec2(e0), vec2(e1), x); }
inline vec3 smoothstep(float e0, float e1, const vec3 &x) { return smoothstep(vec3(e0), vec3(e1), x); }
inline vec4 smoothstep(float e0, float e1, const vec4 &x) { return smoothstep(vec4(e0), vec4(e1), x); }

// mix with a scalar interpolant.
inline vec2 mix(const vec2 &a, const vec2 &b, float t) { return a * (1.0f - t) + b * t; }
inline vec3 mix(const vec3 &a, const vec3 &b, float t) { return a * (1.0f - t) + b * t; }
inline vec4 mix(const vec4 &a, const vec4 &b, float t) { return a * (1.0f - t) + b * t; }

// ---------------------------------------------------------------------------
// geometric functions
// ---------------------------------------------------------------------------

inline float dot(const vec2 &a, const vec2 &b) { return a.x * b.x + a.y * b.y; }
inline float dot(const vec3 &a, const vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float dot(const vec4 &a, const vec4 &b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline float length(float a)       { return fabsf(a); }
inline float length(const vec2 &v) { return sqrtf(dot(v, v)); }
inline float length(const vec3 &v) { return sqrtf(dot(v, v)); }
inline float length(const vec4 &v) { return sqrtf(dot(v, v)); }

inline float distance(const vec2 &a, const vec2 &b) { return length(a - b); }
inline float distance(const vec3 &a, const vec3 &b) { return length(a - b); }
inline float distance(const vec4 &a, const vec4 &b) { return length(a - b); }

// GLSL leaves normalize(0) undefined; returning zero is friendlier and keeps
// NaNs out of the framebuffer.
inline vec2 normalize(const vec2 &v) { float l = length(v); return l > 0.0f ? v / l : vec2(0.0f); }
inline vec3 normalize(const vec3 &v) { float l = length(v); return l > 0.0f ? v / l : vec3(0.0f); }
inline vec4 normalize(const vec4 &v) { float l = length(v); return l > 0.0f ? v / l : vec4(0.0f); }

inline vec3 cross(const vec3 &a, const vec3 &b) {
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline vec2 reflect(const vec2 &i, const vec2 &n) { return i - n * (2.0f * dot(n, i)); }
inline vec3 reflect(const vec3 &i, const vec3 &n) { return i - n * (2.0f * dot(n, i)); }

inline vec3 refract(const vec3 &i, const vec3 &n, float eta) {
    float d = dot(n, i);
    float k = 1.0f - eta * eta * (1.0f - d * d);
    return k < 0.0f ? vec3(0.0f) : (i * eta - n * (eta * d + sqrtf(k)));
}

inline vec3 faceforward(const vec3 &n, const vec3 &i, const vec3 &nref) {
    return dot(nref, i) < 0.0f ? n : -n;
}

// ---------------------------------------------------------------------------
// matrices (column-major, matching GLSL)
// ---------------------------------------------------------------------------

struct mat2 {
    vec2 c0, c1;   // columns

    mat2() : c0(1, 0), c1(0, 1) {}
    explicit mat2(float d) : c0(d, 0), c1(0, d) {}
    // GLSL mat2(a, b, c, d) fills column 0 with (a, b) and column 1 with (c, d).
    mat2(float a, float b, float c, float d) : c0(a, b), c1(c, d) {}
    mat2(const vec2 &col0, const vec2 &col1) : c0(col0), c1(col1) {}

    vec2  operator[](int i) const { return i == 0 ? c0 : c1; }
    vec2 &operator[](int i)       { return i == 0 ? c0 : c1; }
};

inline vec2 operator*(const mat2 &m, const vec2 &v) { return m.c0 * v.x + m.c1 * v.y; }
inline vec2 operator*(const vec2 &v, const mat2 &m) { return vec2(dot(v, m.c0), dot(v, m.c1)); }
inline mat2 operator*(const mat2 &a, const mat2 &b) { return mat2(a * b.c0, a * b.c1); }
inline mat2 operator*(const mat2 &m, float s) { return mat2(m.c0 * s, m.c1 * s); }
inline mat2 operator*(float s, const mat2 &m) { return m * s; }
inline vec2 &operator*=(vec2 &v, const mat2 &m) { v = m * v; return v; }

struct mat3 {
    vec3 c0, c1, c2;

    mat3() : c0(1, 0, 0), c1(0, 1, 0), c2(0, 0, 1) {}
    explicit mat3(float d) : c0(d, 0, 0), c1(0, d, 0), c2(0, 0, d) {}
    mat3(float a, float b, float c, float d, float e, float f, float g, float h, float i)
        : c0(a, b, c), c1(d, e, f), c2(g, h, i) {}
    mat3(const vec3 &col0, const vec3 &col1, const vec3 &col2) : c0(col0), c1(col1), c2(col2) {}

    vec3  operator[](int i) const { return i == 0 ? c0 : (i == 1 ? c1 : c2); }
    vec3 &operator[](int i)       { return i == 0 ? c0 : (i == 1 ? c1 : c2); }
};

inline vec3 operator*(const mat3 &m, const vec3 &v) { return m.c0 * v.x + m.c1 * v.y + m.c2 * v.z; }
inline vec3 operator*(const vec3 &v, const mat3 &m) { return vec3(dot(v, m.c0), dot(v, m.c1), dot(v, m.c2)); }
inline mat3 operator*(const mat3 &a, const mat3 &b) { return mat3(a * b.c0, a * b.c1, a * b.c2); }

inline mat2 transpose(const mat2 &m) { return mat2(m.c0.x, m.c1.x, m.c0.y, m.c1.y); }
inline mat3 transpose(const mat3 &m) {
    return mat3(vec3(m.c0.x, m.c1.x, m.c2.x), vec3(m.c0.y, m.c1.y, m.c2.y), vec3(m.c0.z, m.c1.z, m.c2.z));
}

// ---------------------------------------------------------------------------
// constants and small conveniences
// ---------------------------------------------------------------------------

constexpr float PI      = 3.14159265358979323846f;
constexpr float TAU     = 6.28318530717958647693f;
constexpr float HALF_PI = 1.57079632679489661923f;

/// 2D rotation matrix. Positive `a` rotates counter-clockwise.
inline mat2 rot(float a) {
    float c = cosf(a), s = sinf(a);
    return mat2(c, s, -s, c);
}

/// Saturate to [0, 1]; the most common clamp in shader code.
inline float sat(float a)       { return clamp(a, 0.0f, 1.0f); }
inline vec2  sat(const vec2 &v) { return clamp(v, 0.0f, 1.0f); }
inline vec3  sat(const vec3 &v) { return clamp(v, 0.0f, 1.0f); }
inline vec4  sat(const vec4 &v) { return clamp(v, 0.0f, 1.0f); }

} // namespace pgl

#endif // PGL_HPP
'''


def main():
    parts = [HEAD]

    parts.append(VEC2_CORE)
    parts.append(emit_swizzles("vec2", complete={"vec2"}))
    parts.append("\n};\n\n")

    parts.append(VEC3_CORE)
    parts.append(emit_swizzles("vec3", complete={"vec2", "vec3"}))
    parts.append("\n};\n\n")

    parts.append(VEC4_CORE)
    parts.append(emit_swizzles("vec4", complete={"vec2", "vec3", "vec4"}))
    parts.append("\n};\n")

    parts.append(OPS)
    print("".join(parts), end="")


if __name__ == "__main__":
    main()
