// ===========================================================================
//  json.hpp -- minimal JSON reader and writer, no dependencies.
//
//  Parameters are the interchange format between every tool here: analysis
//  writes them, synthesis reads them, a model emits them, and a human edits
//  them. That makes JSON load-bearing, so it lives in the project rather than
//  in a dependency.
//
//  Scope: RFC 8259 for the subset that appears in parameter files. Objects keep
//  insertion order, because a parameter file that reorders itself on every write
//  is unreadable in a diff. Numbers are doubles.
// ===========================================================================
#ifndef PPM_JSON_HPP
#define PPM_JSON_HPP

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace ppm {
namespace json {

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;

    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;   // ordered

    Value() = default;
    explicit Value(bool b) : type(Bool), boolean(b) {}
    explicit Value(double d) : type(Number), number(d) {}
    explicit Value(const std::string &s) : type(String), string(s) {}

    bool is_null() const { return type == Null; }
    bool is_num() const { return type == Number; }
    bool is_str() const { return type == String; }
    bool is_arr() const { return type == Array; }
    bool is_obj() const { return type == Object; }

    size_t size() const {
        return type == Array ? array.size() : (type == Object ? object.size() : 0);
    }

    /// Member lookup. Returns a shared static null for a missing key, so chained
    /// access like cfg["a"]["b"].num(1.0) never needs a null check.
    const Value &operator[](const std::string &key) const {
        static const Value none;
        if (type != Object) return none;
        for (const auto &kv : object)
            if (kv.first == key) return kv.second;
        return none;
    }

    const Value &operator[](size_t i) const {
        static const Value none;
        if (type != Array || i >= array.size()) return none;
        return array[i];
    }

    bool has(const std::string &key) const {
        if (type != Object) return false;
        for (const auto &kv : object)
            if (kv.first == key) return true;
        return false;
    }

    // Typed reads with a fallback. A parameter file is allowed to omit anything.
    double num(double fallback = 0.0) const { return type == Number ? number : fallback; }
    float flt(float fallback = 0.0f) const {
        return type == Number ? (float)number : fallback;
    }
    int integer(int fallback = 0) const {
        return type == Number ? (int)llround(number) : fallback;
    }
    bool flag(bool fallback = false) const {
        if (type == Bool) return boolean;
        if (type == Number) return number != 0.0;
        return fallback;
    }
    std::string text(const std::string &fallback = "") const {
        return type == String ? string : fallback;
    }

    /// Read as a number whether it is stored as one or as a numeric string.
    ///
    /// ffprobe's JSON emits timings as strings ("0.016667"), so a consumer that
    /// only accepts Number silently reads zero from a perfectly valid document.
    double as_num(double fallback = 0.0) const {
        if (type == Number) return number;
        if (type == String) {
            char *end = nullptr;
            const double d = strtod(string.c_str(), &end);
            if (end && end != string.c_str()) return d;
        }
        if (type == Bool) return boolean ? 1.0 : 0.0;
        return fallback;
    }

    /// Dotted-path lookup: get("layers.0.params.scale").
    /// Numeric components index arrays, everything else indexes objects.
    const Value &get(const std::string &path) const {
        const Value *cur = this;
        size_t start = 0;
        while (start <= path.size()) {
            size_t dot = path.find('.', start);
            if (dot == std::string::npos) dot = path.size();
            const std::string part = path.substr(start, dot - start);
            if (!part.empty()) {
                if (cur->type == Array) {
                    char *end = nullptr;
                    const long idx = strtol(part.c_str(), &end, 10);
                    if (end && *end == '\0' && idx >= 0) cur = &(*cur)[(size_t)idx];
                    else cur = &(*cur)[part];
                } else {
                    cur = &(*cur)[part];
                }
            }
            if (dot == path.size()) break;
            start = dot + 1;
        }
        return *cur;
    }

    void set(const std::string &key, Value v) {
        type = Object;
        for (auto &kv : object) {
            if (kv.first == key) { kv.second = std::move(v); return; }
        }
        object.emplace_back(key, std::move(v));
    }

    void push(Value v) {
        type = Array;
        array.push_back(std::move(v));
    }

    static Value obj() { Value v; v.type = Object; return v; }
    static Value arr() { Value v; v.type = Array; return v; }
    static Value of(double d) { return Value(d); }
    static Value of(const std::string &s) { return Value(s); }
    static Value of(const char *s) { return Value(std::string(s)); }
    static Value of(bool b) { return Value(b); }
};

// ---------------------------------------------------------------------------
// parsing
// ---------------------------------------------------------------------------

namespace detail {

struct Parser {
    const char *p;
    const char *end;
    std::string error;

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    bool fail(const char *msg) {
        if (error.empty()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s at offset %ld", msg, (long)(p - end));
            error = buf;
        }
        return false;
    }

    /// Decode a code point into UTF-8. Surrogate pairs are combined so that
    /// non-BMP characters survive a round trip.
    static void utf8(unsigned cp, std::string &out) {
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    bool hex4(unsigned &out) {
        if (end - p < 4) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = p[i];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        p += 4;
        return true;
    }

    bool str(std::string &out) {
        if (p >= end || *p != '"') return fail("expected string");
        ++p;
        out.clear();
        while (p < end) {
            const char c = *p++;
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }
            if (p >= end) return fail("truncated escape");
            const char e = *p++;
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return fail("bad \\u escape");
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 &&
                        p[0] == '\\' && p[1] == 'u') {
                        const char *save = p;
                        p += 2;
                        unsigned lo;
                        if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else
                            p = save;
                    }
                    utf8(cp, out);
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool value(Value &out) {
        ws();
        if (p >= end) return fail("unexpected end of input");
        switch (*p) {
            case '{': {
                ++p;
                out = Value::obj();
                ws();
                if (p < end && *p == '}') { ++p; return true; }
                for (;;) {
                    ws();
                    std::string key;
                    if (!str(key)) return false;
                    ws();
                    if (p >= end || *p != ':') return fail("expected ':'");
                    ++p;
                    Value v;
                    if (!value(v)) return false;
                    out.object.emplace_back(std::move(key), std::move(v));
                    ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    if (p < end && *p == '}') { ++p; return true; }
                    return fail("expected ',' or '}'");
                }
            }
            case '[': {
                ++p;
                out = Value::arr();
                ws();
                if (p < end && *p == ']') { ++p; return true; }
                for (;;) {
                    Value v;
                    if (!value(v)) return false;
                    out.array.push_back(std::move(v));
                    ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    if (p < end && *p == ']') { ++p; return true; }
                    return fail("expected ',' or ']'");
                }
            }
            case '"': {
                std::string s;
                if (!str(s)) return false;
                out = Value(s);
                return true;
            }
            case 't':
                if (end - p >= 4 && !strncmp(p, "true", 4)) { p += 4; out = Value(true); return true; }
                return fail("bad literal");
            case 'f':
                if (end - p >= 5 && !strncmp(p, "false", 5)) { p += 5; out = Value(false); return true; }
                return fail("bad literal");
            case 'n':
                if (end - p >= 4 && !strncmp(p, "null", 4)) { p += 4; out = Value(); return true; }
                return fail("bad literal");
            default: {
                char *stop = nullptr;
                const double d = strtod(p, &stop);
                if (stop == p) return fail("bad number");
                p = stop;
                out = Value(d);
                return true;
            }
        }
    }
};

} // namespace detail

/// Parse `text`. On failure returns a null Value and fills `error`.
inline Value parse(const std::string &text, std::string &error) {
    detail::Parser ps{text.data(), text.data() + text.size(), ""};
    Value v;
    if (!ps.value(v)) {
        error = ps.error.empty() ? "parse error" : ps.error;
        return Value();
    }
    ps.ws();
    if (ps.p != ps.end) {
        error = "trailing content after JSON value";
        return Value();
    }
    error.clear();
    return v;
}

inline bool parse_file(const std::string &path, Value &out, std::string &error) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open " + path; return false; }
    std::string text;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    out = parse(text, error);
    return error.empty();
}

// ---------------------------------------------------------------------------
// writing
// ---------------------------------------------------------------------------

inline void escape(const std::string &s, std::string &out) {
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back((char)c);
                }
        }
    }
}

/// Format a number compactly but losslessly enough to round-trip a float.
///
/// %.17g would round-trip a double exactly but produces noise like
/// 0.10000000000000001 throughout a parameter file. Since every value here
/// originates as a float, %.9g is exact for our purposes and stays readable.
inline void write_number(double d, std::string &out) {
    if (!std::isfinite(d)) { out += "0"; return; }   // JSON has no NaN/Infinity
    if (d == (double)(long long)d && fabs(d) < 1e15) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)d);
        out += buf;
        return;
    }
    char buf[40];
    snprintf(buf, sizeof(buf), "%.9g", d);
    out += buf;
}

/// Serialise with 2-space indentation. Arrays of numbers are kept on one line,
/// because a 256-bin histogram spread over 256 lines is not readable.
inline void dump(const Value &v, std::string &out, int indent = 0) {
    const std::string pad((size_t)indent * 2, ' ');
    const std::string pad2((size_t)(indent + 1) * 2, ' ');

    switch (v.type) {
        case Value::Null:   out += "null"; break;
        case Value::Bool:   out += v.boolean ? "true" : "false"; break;
        case Value::Number: write_number(v.number, out); break;
        case Value::String: out += '"'; escape(v.string, out); out += '"'; break;
        case Value::Array: {
            if (v.array.empty()) { out += "[]"; break; }
            bool scalar = true;
            for (const Value &e : v.array)
                if (e.type == Value::Array || e.type == Value::Object) { scalar = false; break; }
            if (scalar) {
                out += '[';
                for (size_t i = 0; i < v.array.size(); ++i) {
                    if (i) out += ", ";
                    dump(v.array[i], out, 0);
                }
                out += ']';
            } else {
                out += "[\n";
                for (size_t i = 0; i < v.array.size(); ++i) {
                    out += pad2;
                    dump(v.array[i], out, indent + 1);
                    if (i + 1 < v.array.size()) out += ',';
                    out += '\n';
                }
                out += pad + "]";
            }
            break;
        }
        case Value::Object: {
            if (v.object.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < v.object.size(); ++i) {
                out += pad2 + '"';
                escape(v.object[i].first, out);
                out += "\": ";
                dump(v.object[i].second, out, indent + 1);
                if (i + 1 < v.object.size()) out += ',';
                out += '\n';
            }
            out += pad + "}";
            break;
        }
    }
}

inline std::string dump(const Value &v) {
    std::string out;
    dump(v, out, 0);
    out += '\n';
    return out;
}

// ---------------------------------------------------------------------------
// flat output
// ---------------------------------------------------------------------------
//
// Dotted key=value lines, modelled on ffprobe's `-of flat`. The point is shell
// ergonomics: the output is greppable line by line, and safely `eval`-able
// because every value is quoted, so a consumer needs no JSON parser at all.
//
// Following ffprobe, keys have any character outside [A-Za-z0-9_] replaced with
// '_', and every value is quoted regardless of type.

inline void flat_key(const std::string &in, std::string &out) {
    for (char c : in) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        out.push_back(ok ? c : '_');
    }
}

inline void flat_emit(const Value &v, const std::string &prefix, std::string &out) {
    switch (v.type) {
        case Value::Object:
            for (const auto &kv : v.object) {
                std::string key = prefix;
                if (!key.empty()) key += '.';
                flat_key(kv.first, key);
                flat_emit(kv.second, key, out);
            }
            break;
        case Value::Array:
            for (size_t i = 0; i < v.array.size(); ++i)
                flat_emit(v.array[i], prefix + "." + std::to_string(i), out);
            break;
        default: {
            out += prefix;
            out += "=\"";
            switch (v.type) {
                case Value::Null:   break;                       // empty string
                case Value::Bool:   out += v.boolean ? "1" : "0"; break;
                case Value::Number: write_number(v.number, out); break;
                case Value::String: escape(v.string, out);       break;
                default: break;
            }
            out += "\"\n";
            break;
        }
    }
}

inline std::string dump_flat(const Value &v, const std::string &root = "") {
    std::string out;
    flat_emit(v, root, out);
    return out;
}

inline bool write_flat_file(const std::string &path, const Value &v,
                            const std::string &root = "") {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::string s = dump_flat(v, root);
    const bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
    fclose(f);
    return ok;
}

inline bool write_file(const std::string &path, const Value &v) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::string s = dump(v);
    const bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
    fclose(f);
    return ok;
}

// Small helpers for building arrays of numbers, which analysis output is full of.
inline Value num_array(const std::vector<float> &xs) {
    Value v = Value::arr();
    for (float x : xs) v.push(Value((double)x));
    return v;
}
inline Value num_array(const std::vector<int> &xs) {
    Value v = Value::arr();
    for (int x : xs) v.push(Value((double)x));
    return v;
}
inline Value rgb_array(float r, float g, float b) {
    Value v = Value::arr();
    v.push(Value((double)r));
    v.push(Value((double)g));
    v.push(Value((double)b));
    return v;
}
inline Value xy_array(float x, float y) {
    Value v = Value::arr();
    v.push(Value((double)x));
    v.push(Value((double)y));
    return v;
}
inline Value int_pair(int a, int b) {
    Value v = Value::arr();
    v.push(Value((double)a));
    v.push(Value((double)b));
    return v;
}

} // namespace json
} // namespace ppm

#endif // PPM_JSON_HPP
