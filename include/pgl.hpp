// ===========================================================================
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

struct vec2 {
    float x, y;

    vec2() : x(0), y(0) {}
    explicit vec2(float s) : x(s), y(s) {}
    vec2(float x_, float y_) : x(x_), y(y_) {}

    float  operator[](int i) const { return i == 0 ? x : y; }
    float &operator[](int i)       { return i == 0 ? x : y; }

    float g() const { return y; }
    template<class V = vec3> V ggg() const { return V(y, y, y); }
    template<class V = vec4> V gggg() const { return V(y, y, y, y); }
    float r() const { return x; }
    vec2 rg() const { return vec2(x, y); }
    template<class V = vec3> V rrr() const { return V(x, x, x); }
    template<class V = vec4> V rrrr() const { return V(x, x, x, x); }
    vec2 xx() const { return vec2(x, x); }
    template<class V = vec3> V xxx() const { return V(x, x, x); }
    template<class V = vec4> V xxxx() const { return V(x, x, x, x); }
    template<class V = vec4> V xxxy() const { return V(x, x, x, y); }
    template<class V = vec3> V xxy() const { return V(x, x, y); }
    template<class V = vec4> V xxyx() const { return V(x, x, y, x); }
    template<class V = vec4> V xxyy() const { return V(x, x, y, y); }
    vec2 xy() const { return vec2(x, y); }
    template<class V = vec3> V xyx() const { return V(x, y, x); }
    template<class V = vec4> V xyxx() const { return V(x, y, x, x); }
    template<class V = vec4> V xyxy() const { return V(x, y, x, y); }
    template<class V = vec3> V xyy() const { return V(x, y, y); }
    template<class V = vec4> V xyyx() const { return V(x, y, y, x); }
    template<class V = vec4> V xyyy() const { return V(x, y, y, y); }
    vec2 yx() const { return vec2(y, x); }
    template<class V = vec3> V yxx() const { return V(y, x, x); }
    template<class V = vec4> V yxxx() const { return V(y, x, x, x); }
    template<class V = vec4> V yxxy() const { return V(y, x, x, y); }
    template<class V = vec3> V yxy() const { return V(y, x, y); }
    template<class V = vec4> V yxyx() const { return V(y, x, y, x); }
    template<class V = vec4> V yxyy() const { return V(y, x, y, y); }
    vec2 yy() const { return vec2(y, y); }
    template<class V = vec3> V yyx() const { return V(y, y, x); }
    template<class V = vec4> V yyxx() const { return V(y, y, x, x); }
    template<class V = vec4> V yyxy() const { return V(y, y, x, y); }
    template<class V = vec3> V yyy() const { return V(y, y, y); }
    template<class V = vec4> V yyyx() const { return V(y, y, y, x); }
    template<class V = vec4> V yyyy() const { return V(y, y, y, y); }
};

struct vec3 {
    float x, y, z;

    vec3() : x(0), y(0), z(0) {}
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    vec3(const vec2 &a, float z_) : x(a.x), y(a.y), z(z_) {}
    vec3(float x_, const vec2 &a) : x(x_), y(a.x), z(a.y) {}

    float  operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float &operator[](int i)       { return i == 0 ? x : (i == 1 ? y : z); }

    float b() const { return z; }
    vec3 bbb() const { return vec3(z, z, z); }
    template<class V = vec4> V bbbb() const { return V(z, z, z, z); }
    vec3 bgr() const { return vec3(z, y, x); }
    float g() const { return y; }
    vec2 gb() const { return vec2(y, z); }
    vec3 ggg() const { return vec3(y, y, y); }
    template<class V = vec4> V gggg() const { return V(y, y, y, y); }
    float r() const { return x; }
    vec2 rg() const { return vec2(x, y); }
    vec3 rgb() const { return vec3(x, y, z); }
    template<class V = vec4> V rgbr() const { return V(x, y, z, x); }
    vec3 rrr() const { return vec3(x, x, x); }
    template<class V = vec4> V rrrr() const { return V(x, x, x, x); }
    vec2 xx() const { return vec2(x, x); }
    vec3 xxx() const { return vec3(x, x, x); }
    template<class V = vec4> V xxxx() const { return V(x, x, x, x); }
    template<class V = vec4> V xxxy() const { return V(x, x, x, y); }
    template<class V = vec4> V xxxz() const { return V(x, x, x, z); }
    vec3 xxy() const { return vec3(x, x, y); }
    template<class V = vec4> V xxyx() const { return V(x, x, y, x); }
    template<class V = vec4> V xxyy() const { return V(x, x, y, y); }
    template<class V = vec4> V xxyz() const { return V(x, x, y, z); }
    vec3 xxz() const { return vec3(x, x, z); }
    template<class V = vec4> V xxzx() const { return V(x, x, z, x); }
    template<class V = vec4> V xxzy() const { return V(x, x, z, y); }
    template<class V = vec4> V xxzz() const { return V(x, x, z, z); }
    vec2 xy() const { return vec2(x, y); }
    vec3 xyx() const { return vec3(x, y, x); }
    template<class V = vec4> V xyxx() const { return V(x, y, x, x); }
    template<class V = vec4> V xyxy() const { return V(x, y, x, y); }
    template<class V = vec4> V xyxz() const { return V(x, y, x, z); }
    vec3 xyy() const { return vec3(x, y, y); }
    template<class V = vec4> V xyyx() const { return V(x, y, y, x); }
    template<class V = vec4> V xyyy() const { return V(x, y, y, y); }
    template<class V = vec4> V xyyz() const { return V(x, y, y, z); }
    vec3 xyz() const { return vec3(x, y, z); }
    template<class V = vec4> V xyzx() const { return V(x, y, z, x); }
    template<class V = vec4> V xyzy() const { return V(x, y, z, y); }
    template<class V = vec4> V xyzz() const { return V(x, y, z, z); }
    vec2 xz() const { return vec2(x, z); }
    vec3 xzx() const { return vec3(x, z, x); }
    template<class V = vec4> V xzxx() const { return V(x, z, x, x); }
    template<class V = vec4> V xzxy() const { return V(x, z, x, y); }
    template<class V = vec4> V xzxz() const { return V(x, z, x, z); }
    vec3 xzy() const { return vec3(x, z, y); }
    template<class V = vec4> V xzyx() const { return V(x, z, y, x); }
    template<class V = vec4> V xzyy() const { return V(x, z, y, y); }
    template<class V = vec4> V xzyz() const { return V(x, z, y, z); }
    vec3 xzz() const { return vec3(x, z, z); }
    template<class V = vec4> V xzzx() const { return V(x, z, z, x); }
    template<class V = vec4> V xzzy() const { return V(x, z, z, y); }
    template<class V = vec4> V xzzz() const { return V(x, z, z, z); }
    vec2 yx() const { return vec2(y, x); }
    vec3 yxx() const { return vec3(y, x, x); }
    template<class V = vec4> V yxxx() const { return V(y, x, x, x); }
    template<class V = vec4> V yxxy() const { return V(y, x, x, y); }
    template<class V = vec4> V yxxz() const { return V(y, x, x, z); }
    vec3 yxy() const { return vec3(y, x, y); }
    template<class V = vec4> V yxyx() const { return V(y, x, y, x); }
    template<class V = vec4> V yxyy() const { return V(y, x, y, y); }
    template<class V = vec4> V yxyz() const { return V(y, x, y, z); }
    vec3 yxz() const { return vec3(y, x, z); }
    template<class V = vec4> V yxzx() const { return V(y, x, z, x); }
    template<class V = vec4> V yxzy() const { return V(y, x, z, y); }
    template<class V = vec4> V yxzz() const { return V(y, x, z, z); }
    vec2 yy() const { return vec2(y, y); }
    vec3 yyx() const { return vec3(y, y, x); }
    template<class V = vec4> V yyxx() const { return V(y, y, x, x); }
    template<class V = vec4> V yyxy() const { return V(y, y, x, y); }
    template<class V = vec4> V yyxz() const { return V(y, y, x, z); }
    vec3 yyy() const { return vec3(y, y, y); }
    template<class V = vec4> V yyyx() const { return V(y, y, y, x); }
    template<class V = vec4> V yyyy() const { return V(y, y, y, y); }
    template<class V = vec4> V yyyz() const { return V(y, y, y, z); }
    vec3 yyz() const { return vec3(y, y, z); }
    template<class V = vec4> V yyzx() const { return V(y, y, z, x); }
    template<class V = vec4> V yyzy() const { return V(y, y, z, y); }
    template<class V = vec4> V yyzz() const { return V(y, y, z, z); }
    vec2 yz() const { return vec2(y, z); }
    vec3 yzx() const { return vec3(y, z, x); }
    template<class V = vec4> V yzxx() const { return V(y, z, x, x); }
    template<class V = vec4> V yzxy() const { return V(y, z, x, y); }
    template<class V = vec4> V yzxz() const { return V(y, z, x, z); }
    vec3 yzy() const { return vec3(y, z, y); }
    template<class V = vec4> V yzyx() const { return V(y, z, y, x); }
    template<class V = vec4> V yzyy() const { return V(y, z, y, y); }
    template<class V = vec4> V yzyz() const { return V(y, z, y, z); }
    vec3 yzz() const { return vec3(y, z, z); }
    template<class V = vec4> V yzzx() const { return V(y, z, z, x); }
    template<class V = vec4> V yzzy() const { return V(y, z, z, y); }
    template<class V = vec4> V yzzz() const { return V(y, z, z, z); }
    vec2 zx() const { return vec2(z, x); }
    vec3 zxx() const { return vec3(z, x, x); }
    template<class V = vec4> V zxxx() const { return V(z, x, x, x); }
    template<class V = vec4> V zxxy() const { return V(z, x, x, y); }
    template<class V = vec4> V zxxz() const { return V(z, x, x, z); }
    vec3 zxy() const { return vec3(z, x, y); }
    template<class V = vec4> V zxyx() const { return V(z, x, y, x); }
    template<class V = vec4> V zxyy() const { return V(z, x, y, y); }
    template<class V = vec4> V zxyz() const { return V(z, x, y, z); }
    vec3 zxz() const { return vec3(z, x, z); }
    template<class V = vec4> V zxzx() const { return V(z, x, z, x); }
    template<class V = vec4> V zxzy() const { return V(z, x, z, y); }
    template<class V = vec4> V zxzz() const { return V(z, x, z, z); }
    vec2 zy() const { return vec2(z, y); }
    vec3 zyx() const { return vec3(z, y, x); }
    template<class V = vec4> V zyxx() const { return V(z, y, x, x); }
    template<class V = vec4> V zyxy() const { return V(z, y, x, y); }
    template<class V = vec4> V zyxz() const { return V(z, y, x, z); }
    vec3 zyy() const { return vec3(z, y, y); }
    template<class V = vec4> V zyyx() const { return V(z, y, y, x); }
    template<class V = vec4> V zyyy() const { return V(z, y, y, y); }
    template<class V = vec4> V zyyz() const { return V(z, y, y, z); }
    vec3 zyz() const { return vec3(z, y, z); }
    template<class V = vec4> V zyzx() const { return V(z, y, z, x); }
    template<class V = vec4> V zyzy() const { return V(z, y, z, y); }
    template<class V = vec4> V zyzz() const { return V(z, y, z, z); }
    vec2 zz() const { return vec2(z, z); }
    vec3 zzx() const { return vec3(z, z, x); }
    template<class V = vec4> V zzxx() const { return V(z, z, x, x); }
    template<class V = vec4> V zzxy() const { return V(z, z, x, y); }
    template<class V = vec4> V zzxz() const { return V(z, z, x, z); }
    vec3 zzy() const { return vec3(z, z, y); }
    template<class V = vec4> V zzyx() const { return V(z, z, y, x); }
    template<class V = vec4> V zzyy() const { return V(z, z, y, y); }
    template<class V = vec4> V zzyz() const { return V(z, z, y, z); }
    vec3 zzz() const { return vec3(z, z, z); }
    template<class V = vec4> V zzzx() const { return V(z, z, z, x); }
    template<class V = vec4> V zzzy() const { return V(z, z, z, y); }
    template<class V = vec4> V zzzz() const { return V(z, z, z, z); }
};

struct vec4 {
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

    float a() const { return w; }
    vec3 aaa() const { return vec3(w, w, w); }
    vec4 aaaa() const { return vec4(w, w, w, w); }
    vec4 abgr() const { return vec4(w, z, y, x); }
    vec4 argb() const { return vec4(w, x, y, z); }
    float b() const { return z; }
    vec2 ba() const { return vec2(z, w); }
    vec3 bbb() const { return vec3(z, z, z); }
    vec4 bbbb() const { return vec4(z, z, z, z); }
    vec3 bgr() const { return vec3(z, y, x); }
    vec4 bgra() const { return vec4(z, y, x, w); }
    float g() const { return y; }
    vec2 gb() const { return vec2(y, z); }
    vec3 ggg() const { return vec3(y, y, y); }
    vec4 gggg() const { return vec4(y, y, y, y); }
    float r() const { return x; }
    vec2 rg() const { return vec2(x, y); }
    vec3 rgb() const { return vec3(x, y, z); }
    vec4 rgba() const { return vec4(x, y, z, w); }
    vec4 rgbr() const { return vec4(x, y, z, x); }
    vec3 rrr() const { return vec3(x, x, x); }
    vec4 rrrr() const { return vec4(x, x, x, x); }
    vec2 ww() const { return vec2(w, w); }
    vec3 www() const { return vec3(w, w, w); }
    vec4 wwww() const { return vec4(w, w, w, w); }
    vec4 wwwx() const { return vec4(w, w, w, x); }
    vec4 wwwy() const { return vec4(w, w, w, y); }
    vec4 wwwz() const { return vec4(w, w, w, z); }
    vec3 wwx() const { return vec3(w, w, x); }
    vec4 wwxw() const { return vec4(w, w, x, w); }
    vec4 wwxx() const { return vec4(w, w, x, x); }
    vec4 wwxy() const { return vec4(w, w, x, y); }
    vec4 wwxz() const { return vec4(w, w, x, z); }
    vec3 wwy() const { return vec3(w, w, y); }
    vec4 wwyw() const { return vec4(w, w, y, w); }
    vec4 wwyx() const { return vec4(w, w, y, x); }
    vec4 wwyy() const { return vec4(w, w, y, y); }
    vec4 wwyz() const { return vec4(w, w, y, z); }
    vec3 wwz() const { return vec3(w, w, z); }
    vec4 wwzw() const { return vec4(w, w, z, w); }
    vec4 wwzx() const { return vec4(w, w, z, x); }
    vec4 wwzy() const { return vec4(w, w, z, y); }
    vec4 wwzz() const { return vec4(w, w, z, z); }
    vec2 wx() const { return vec2(w, x); }
    vec3 wxw() const { return vec3(w, x, w); }
    vec4 wxww() const { return vec4(w, x, w, w); }
    vec4 wxwx() const { return vec4(w, x, w, x); }
    vec4 wxwy() const { return vec4(w, x, w, y); }
    vec4 wxwz() const { return vec4(w, x, w, z); }
    vec3 wxx() const { return vec3(w, x, x); }
    vec4 wxxw() const { return vec4(w, x, x, w); }
    vec4 wxxx() const { return vec4(w, x, x, x); }
    vec4 wxxy() const { return vec4(w, x, x, y); }
    vec4 wxxz() const { return vec4(w, x, x, z); }
    vec3 wxy() const { return vec3(w, x, y); }
    vec4 wxyw() const { return vec4(w, x, y, w); }
    vec4 wxyx() const { return vec4(w, x, y, x); }
    vec4 wxyy() const { return vec4(w, x, y, y); }
    vec4 wxyz() const { return vec4(w, x, y, z); }
    vec3 wxz() const { return vec3(w, x, z); }
    vec4 wxzw() const { return vec4(w, x, z, w); }
    vec4 wxzx() const { return vec4(w, x, z, x); }
    vec4 wxzy() const { return vec4(w, x, z, y); }
    vec4 wxzz() const { return vec4(w, x, z, z); }
    vec2 wy() const { return vec2(w, y); }
    vec3 wyw() const { return vec3(w, y, w); }
    vec4 wyww() const { return vec4(w, y, w, w); }
    vec4 wywx() const { return vec4(w, y, w, x); }
    vec4 wywy() const { return vec4(w, y, w, y); }
    vec4 wywz() const { return vec4(w, y, w, z); }
    vec3 wyx() const { return vec3(w, y, x); }
    vec4 wyxw() const { return vec4(w, y, x, w); }
    vec4 wyxx() const { return vec4(w, y, x, x); }
    vec4 wyxy() const { return vec4(w, y, x, y); }
    vec4 wyxz() const { return vec4(w, y, x, z); }
    vec3 wyy() const { return vec3(w, y, y); }
    vec4 wyyw() const { return vec4(w, y, y, w); }
    vec4 wyyx() const { return vec4(w, y, y, x); }
    vec4 wyyy() const { return vec4(w, y, y, y); }
    vec4 wyyz() const { return vec4(w, y, y, z); }
    vec3 wyz() const { return vec3(w, y, z); }
    vec4 wyzw() const { return vec4(w, y, z, w); }
    vec4 wyzx() const { return vec4(w, y, z, x); }
    vec4 wyzy() const { return vec4(w, y, z, y); }
    vec4 wyzz() const { return vec4(w, y, z, z); }
    vec2 wz() const { return vec2(w, z); }
    vec3 wzw() const { return vec3(w, z, w); }
    vec4 wzww() const { return vec4(w, z, w, w); }
    vec4 wzwx() const { return vec4(w, z, w, x); }
    vec4 wzwy() const { return vec4(w, z, w, y); }
    vec4 wzwz() const { return vec4(w, z, w, z); }
    vec3 wzx() const { return vec3(w, z, x); }
    vec4 wzxw() const { return vec4(w, z, x, w); }
    vec4 wzxx() const { return vec4(w, z, x, x); }
    vec4 wzxy() const { return vec4(w, z, x, y); }
    vec4 wzxz() const { return vec4(w, z, x, z); }
    vec3 wzy() const { return vec3(w, z, y); }
    vec4 wzyw() const { return vec4(w, z, y, w); }
    vec4 wzyx() const { return vec4(w, z, y, x); }
    vec4 wzyy() const { return vec4(w, z, y, y); }
    vec4 wzyz() const { return vec4(w, z, y, z); }
    vec3 wzz() const { return vec3(w, z, z); }
    vec4 wzzw() const { return vec4(w, z, z, w); }
    vec4 wzzx() const { return vec4(w, z, z, x); }
    vec4 wzzy() const { return vec4(w, z, z, y); }
    vec4 wzzz() const { return vec4(w, z, z, z); }
    vec2 xw() const { return vec2(x, w); }
    vec3 xww() const { return vec3(x, w, w); }
    vec4 xwww() const { return vec4(x, w, w, w); }
    vec4 xwwx() const { return vec4(x, w, w, x); }
    vec4 xwwy() const { return vec4(x, w, w, y); }
    vec4 xwwz() const { return vec4(x, w, w, z); }
    vec3 xwx() const { return vec3(x, w, x); }
    vec4 xwxw() const { return vec4(x, w, x, w); }
    vec4 xwxx() const { return vec4(x, w, x, x); }
    vec4 xwxy() const { return vec4(x, w, x, y); }
    vec4 xwxz() const { return vec4(x, w, x, z); }
    vec3 xwy() const { return vec3(x, w, y); }
    vec4 xwyw() const { return vec4(x, w, y, w); }
    vec4 xwyx() const { return vec4(x, w, y, x); }
    vec4 xwyy() const { return vec4(x, w, y, y); }
    vec4 xwyz() const { return vec4(x, w, y, z); }
    vec3 xwz() const { return vec3(x, w, z); }
    vec4 xwzw() const { return vec4(x, w, z, w); }
    vec4 xwzx() const { return vec4(x, w, z, x); }
    vec4 xwzy() const { return vec4(x, w, z, y); }
    vec4 xwzz() const { return vec4(x, w, z, z); }
    vec2 xx() const { return vec2(x, x); }
    vec3 xxw() const { return vec3(x, x, w); }
    vec4 xxww() const { return vec4(x, x, w, w); }
    vec4 xxwx() const { return vec4(x, x, w, x); }
    vec4 xxwy() const { return vec4(x, x, w, y); }
    vec4 xxwz() const { return vec4(x, x, w, z); }
    vec3 xxx() const { return vec3(x, x, x); }
    vec4 xxxw() const { return vec4(x, x, x, w); }
    vec4 xxxx() const { return vec4(x, x, x, x); }
    vec4 xxxy() const { return vec4(x, x, x, y); }
    vec4 xxxz() const { return vec4(x, x, x, z); }
    vec3 xxy() const { return vec3(x, x, y); }
    vec4 xxyw() const { return vec4(x, x, y, w); }
    vec4 xxyx() const { return vec4(x, x, y, x); }
    vec4 xxyy() const { return vec4(x, x, y, y); }
    vec4 xxyz() const { return vec4(x, x, y, z); }
    vec3 xxz() const { return vec3(x, x, z); }
    vec4 xxzw() const { return vec4(x, x, z, w); }
    vec4 xxzx() const { return vec4(x, x, z, x); }
    vec4 xxzy() const { return vec4(x, x, z, y); }
    vec4 xxzz() const { return vec4(x, x, z, z); }
    vec2 xy() const { return vec2(x, y); }
    vec3 xyw() const { return vec3(x, y, w); }
    vec4 xyww() const { return vec4(x, y, w, w); }
    vec4 xywx() const { return vec4(x, y, w, x); }
    vec4 xywy() const { return vec4(x, y, w, y); }
    vec4 xywz() const { return vec4(x, y, w, z); }
    vec3 xyx() const { return vec3(x, y, x); }
    vec4 xyxw() const { return vec4(x, y, x, w); }
    vec4 xyxx() const { return vec4(x, y, x, x); }
    vec4 xyxy() const { return vec4(x, y, x, y); }
    vec4 xyxz() const { return vec4(x, y, x, z); }
    vec3 xyy() const { return vec3(x, y, y); }
    vec4 xyyw() const { return vec4(x, y, y, w); }
    vec4 xyyx() const { return vec4(x, y, y, x); }
    vec4 xyyy() const { return vec4(x, y, y, y); }
    vec4 xyyz() const { return vec4(x, y, y, z); }
    vec3 xyz() const { return vec3(x, y, z); }
    vec4 xyzw() const { return vec4(x, y, z, w); }
    vec4 xyzx() const { return vec4(x, y, z, x); }
    vec4 xyzy() const { return vec4(x, y, z, y); }
    vec4 xyzz() const { return vec4(x, y, z, z); }
    vec2 xz() const { return vec2(x, z); }
    vec3 xzw() const { return vec3(x, z, w); }
    vec4 xzww() const { return vec4(x, z, w, w); }
    vec4 xzwx() const { return vec4(x, z, w, x); }
    vec4 xzwy() const { return vec4(x, z, w, y); }
    vec4 xzwz() const { return vec4(x, z, w, z); }
    vec3 xzx() const { return vec3(x, z, x); }
    vec4 xzxw() const { return vec4(x, z, x, w); }
    vec4 xzxx() const { return vec4(x, z, x, x); }
    vec4 xzxy() const { return vec4(x, z, x, y); }
    vec4 xzxz() const { return vec4(x, z, x, z); }
    vec3 xzy() const { return vec3(x, z, y); }
    vec4 xzyw() const { return vec4(x, z, y, w); }
    vec4 xzyx() const { return vec4(x, z, y, x); }
    vec4 xzyy() const { return vec4(x, z, y, y); }
    vec4 xzyz() const { return vec4(x, z, y, z); }
    vec3 xzz() const { return vec3(x, z, z); }
    vec4 xzzw() const { return vec4(x, z, z, w); }
    vec4 xzzx() const { return vec4(x, z, z, x); }
    vec4 xzzy() const { return vec4(x, z, z, y); }
    vec4 xzzz() const { return vec4(x, z, z, z); }
    vec2 yw() const { return vec2(y, w); }
    vec3 yww() const { return vec3(y, w, w); }
    vec4 ywww() const { return vec4(y, w, w, w); }
    vec4 ywwx() const { return vec4(y, w, w, x); }
    vec4 ywwy() const { return vec4(y, w, w, y); }
    vec4 ywwz() const { return vec4(y, w, w, z); }
    vec3 ywx() const { return vec3(y, w, x); }
    vec4 ywxw() const { return vec4(y, w, x, w); }
    vec4 ywxx() const { return vec4(y, w, x, x); }
    vec4 ywxy() const { return vec4(y, w, x, y); }
    vec4 ywxz() const { return vec4(y, w, x, z); }
    vec3 ywy() const { return vec3(y, w, y); }
    vec4 ywyw() const { return vec4(y, w, y, w); }
    vec4 ywyx() const { return vec4(y, w, y, x); }
    vec4 ywyy() const { return vec4(y, w, y, y); }
    vec4 ywyz() const { return vec4(y, w, y, z); }
    vec3 ywz() const { return vec3(y, w, z); }
    vec4 ywzw() const { return vec4(y, w, z, w); }
    vec4 ywzx() const { return vec4(y, w, z, x); }
    vec4 ywzy() const { return vec4(y, w, z, y); }
    vec4 ywzz() const { return vec4(y, w, z, z); }
    vec2 yx() const { return vec2(y, x); }
    vec3 yxw() const { return vec3(y, x, w); }
    vec4 yxww() const { return vec4(y, x, w, w); }
    vec4 yxwx() const { return vec4(y, x, w, x); }
    vec4 yxwy() const { return vec4(y, x, w, y); }
    vec4 yxwz() const { return vec4(y, x, w, z); }
    vec3 yxx() const { return vec3(y, x, x); }
    vec4 yxxw() const { return vec4(y, x, x, w); }
    vec4 yxxx() const { return vec4(y, x, x, x); }
    vec4 yxxy() const { return vec4(y, x, x, y); }
    vec4 yxxz() const { return vec4(y, x, x, z); }
    vec3 yxy() const { return vec3(y, x, y); }
    vec4 yxyw() const { return vec4(y, x, y, w); }
    vec4 yxyx() const { return vec4(y, x, y, x); }
    vec4 yxyy() const { return vec4(y, x, y, y); }
    vec4 yxyz() const { return vec4(y, x, y, z); }
    vec3 yxz() const { return vec3(y, x, z); }
    vec4 yxzw() const { return vec4(y, x, z, w); }
    vec4 yxzx() const { return vec4(y, x, z, x); }
    vec4 yxzy() const { return vec4(y, x, z, y); }
    vec4 yxzz() const { return vec4(y, x, z, z); }
    vec2 yy() const { return vec2(y, y); }
    vec3 yyw() const { return vec3(y, y, w); }
    vec4 yyww() const { return vec4(y, y, w, w); }
    vec4 yywx() const { return vec4(y, y, w, x); }
    vec4 yywy() const { return vec4(y, y, w, y); }
    vec4 yywz() const { return vec4(y, y, w, z); }
    vec3 yyx() const { return vec3(y, y, x); }
    vec4 yyxw() const { return vec4(y, y, x, w); }
    vec4 yyxx() const { return vec4(y, y, x, x); }
    vec4 yyxy() const { return vec4(y, y, x, y); }
    vec4 yyxz() const { return vec4(y, y, x, z); }
    vec3 yyy() const { return vec3(y, y, y); }
    vec4 yyyw() const { return vec4(y, y, y, w); }
    vec4 yyyx() const { return vec4(y, y, y, x); }
    vec4 yyyy() const { return vec4(y, y, y, y); }
    vec4 yyyz() const { return vec4(y, y, y, z); }
    vec3 yyz() const { return vec3(y, y, z); }
    vec4 yyzw() const { return vec4(y, y, z, w); }
    vec4 yyzx() const { return vec4(y, y, z, x); }
    vec4 yyzy() const { return vec4(y, y, z, y); }
    vec4 yyzz() const { return vec4(y, y, z, z); }
    vec2 yz() const { return vec2(y, z); }
    vec3 yzw() const { return vec3(y, z, w); }
    vec4 yzww() const { return vec4(y, z, w, w); }
    vec4 yzwx() const { return vec4(y, z, w, x); }
    vec4 yzwy() const { return vec4(y, z, w, y); }
    vec4 yzwz() const { return vec4(y, z, w, z); }
    vec3 yzx() const { return vec3(y, z, x); }
    vec4 yzxw() const { return vec4(y, z, x, w); }
    vec4 yzxx() const { return vec4(y, z, x, x); }
    vec4 yzxy() const { return vec4(y, z, x, y); }
    vec4 yzxz() const { return vec4(y, z, x, z); }
    vec3 yzy() const { return vec3(y, z, y); }
    vec4 yzyw() const { return vec4(y, z, y, w); }
    vec4 yzyx() const { return vec4(y, z, y, x); }
    vec4 yzyy() const { return vec4(y, z, y, y); }
    vec4 yzyz() const { return vec4(y, z, y, z); }
    vec3 yzz() const { return vec3(y, z, z); }
    vec4 yzzw() const { return vec4(y, z, z, w); }
    vec4 yzzx() const { return vec4(y, z, z, x); }
    vec4 yzzy() const { return vec4(y, z, z, y); }
    vec4 yzzz() const { return vec4(y, z, z, z); }
    vec2 zw() const { return vec2(z, w); }
    vec3 zww() const { return vec3(z, w, w); }
    vec4 zwww() const { return vec4(z, w, w, w); }
    vec4 zwwx() const { return vec4(z, w, w, x); }
    vec4 zwwy() const { return vec4(z, w, w, y); }
    vec4 zwwz() const { return vec4(z, w, w, z); }
    vec3 zwx() const { return vec3(z, w, x); }
    vec4 zwxw() const { return vec4(z, w, x, w); }
    vec4 zwxx() const { return vec4(z, w, x, x); }
    vec4 zwxy() const { return vec4(z, w, x, y); }
    vec4 zwxz() const { return vec4(z, w, x, z); }
    vec3 zwy() const { return vec3(z, w, y); }
    vec4 zwyw() const { return vec4(z, w, y, w); }
    vec4 zwyx() const { return vec4(z, w, y, x); }
    vec4 zwyy() const { return vec4(z, w, y, y); }
    vec4 zwyz() const { return vec4(z, w, y, z); }
    vec3 zwz() const { return vec3(z, w, z); }
    vec4 zwzw() const { return vec4(z, w, z, w); }
    vec4 zwzx() const { return vec4(z, w, z, x); }
    vec4 zwzy() const { return vec4(z, w, z, y); }
    vec4 zwzz() const { return vec4(z, w, z, z); }
    vec2 zx() const { return vec2(z, x); }
    vec3 zxw() const { return vec3(z, x, w); }
    vec4 zxww() const { return vec4(z, x, w, w); }
    vec4 zxwx() const { return vec4(z, x, w, x); }
    vec4 zxwy() const { return vec4(z, x, w, y); }
    vec4 zxwz() const { return vec4(z, x, w, z); }
    vec3 zxx() const { return vec3(z, x, x); }
    vec4 zxxw() const { return vec4(z, x, x, w); }
    vec4 zxxx() const { return vec4(z, x, x, x); }
    vec4 zxxy() const { return vec4(z, x, x, y); }
    vec4 zxxz() const { return vec4(z, x, x, z); }
    vec3 zxy() const { return vec3(z, x, y); }
    vec4 zxyw() const { return vec4(z, x, y, w); }
    vec4 zxyx() const { return vec4(z, x, y, x); }
    vec4 zxyy() const { return vec4(z, x, y, y); }
    vec4 zxyz() const { return vec4(z, x, y, z); }
    vec3 zxz() const { return vec3(z, x, z); }
    vec4 zxzw() const { return vec4(z, x, z, w); }
    vec4 zxzx() const { return vec4(z, x, z, x); }
    vec4 zxzy() const { return vec4(z, x, z, y); }
    vec4 zxzz() const { return vec4(z, x, z, z); }
    vec2 zy() const { return vec2(z, y); }
    vec3 zyw() const { return vec3(z, y, w); }
    vec4 zyww() const { return vec4(z, y, w, w); }
    vec4 zywx() const { return vec4(z, y, w, x); }
    vec4 zywy() const { return vec4(z, y, w, y); }
    vec4 zywz() const { return vec4(z, y, w, z); }
    vec3 zyx() const { return vec3(z, y, x); }
    vec4 zyxw() const { return vec4(z, y, x, w); }
    vec4 zyxx() const { return vec4(z, y, x, x); }
    vec4 zyxy() const { return vec4(z, y, x, y); }
    vec4 zyxz() const { return vec4(z, y, x, z); }
    vec3 zyy() const { return vec3(z, y, y); }
    vec4 zyyw() const { return vec4(z, y, y, w); }
    vec4 zyyx() const { return vec4(z, y, y, x); }
    vec4 zyyy() const { return vec4(z, y, y, y); }
    vec4 zyyz() const { return vec4(z, y, y, z); }
    vec3 zyz() const { return vec3(z, y, z); }
    vec4 zyzw() const { return vec4(z, y, z, w); }
    vec4 zyzx() const { return vec4(z, y, z, x); }
    vec4 zyzy() const { return vec4(z, y, z, y); }
    vec4 zyzz() const { return vec4(z, y, z, z); }
    vec2 zz() const { return vec2(z, z); }
    vec3 zzw() const { return vec3(z, z, w); }
    vec4 zzww() const { return vec4(z, z, w, w); }
    vec4 zzwx() const { return vec4(z, z, w, x); }
    vec4 zzwy() const { return vec4(z, z, w, y); }
    vec4 zzwz() const { return vec4(z, z, w, z); }
    vec3 zzx() const { return vec3(z, z, x); }
    vec4 zzxw() const { return vec4(z, z, x, w); }
    vec4 zzxx() const { return vec4(z, z, x, x); }
    vec4 zzxy() const { return vec4(z, z, x, y); }
    vec4 zzxz() const { return vec4(z, z, x, z); }
    vec3 zzy() const { return vec3(z, z, y); }
    vec4 zzyw() const { return vec4(z, z, y, w); }
    vec4 zzyx() const { return vec4(z, z, y, x); }
    vec4 zzyy() const { return vec4(z, z, y, y); }
    vec4 zzyz() const { return vec4(z, z, y, z); }
    vec3 zzz() const { return vec3(z, z, z); }
    vec4 zzzw() const { return vec4(z, z, z, w); }
    vec4 zzzx() const { return vec4(z, z, z, x); }
    vec4 zzzy() const { return vec4(z, z, z, y); }
    vec4 zzzz() const { return vec4(z, z, z, z); }
};

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
