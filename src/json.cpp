// Minimal JSON parser/serializer.
#include "m2rig/json.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace m2rig::json {

const Value* Value::find(const std::string& key) const {
    if (type != Type::Object || !obj) return nullptr;
    auto it = obj->find(key);
    return it == obj->end() ? nullptr : &it->second;
}

std::string Value::getString(const std::string& key, const std::string& fallback) const {
    const Value* v = find(key);
    return (v && v->type == Type::String) ? v->str : fallback;
}

double Value::getNumber(const std::string& key, double fallback) const {
    const Value* v = find(key);
    return (v && v->type == Type::Number) ? v->number : fallback;
}

bool Value::getBool(const std::string& key, bool fallback) const {
    const Value* v = find(key);
    return (v && v->type == Type::Bool) ? v->boolean : fallback;
}

namespace {

struct Parser {
    const char* cur;
    const char* end;
    std::string asset;
    std::string error;

    void skipWs() {
        while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\n' || *cur == '\r')) ++cur;
    }
    bool consume(char c) {
        skipWs();
        if (cur < end && *cur == c) {
            ++cur;
            return true;
        }
        return false;
    }
    bool parseValue(Value& out, int depth) {
        if (depth > 256) {
            error = "nesting too deep";
            return false;
        }
        skipWs();
        if (cur >= end) {
            error = "unexpected end of input";
            return false;
        }
        switch (*cur) {
            case 'n': return parseLiteral("null", out, Value::makeNull());
            case 't': {
                Value v;
                if (!parseLiteral("true", out, Value::makeBool(true))) return false;
                (void)v;
                return true;
            }
            case 'f': {
                Value v;
                if (!parseLiteral("false", out, Value::makeBool(false))) return false;
                (void)v;
                return true;
            }
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = Value::makeString(std::move(s));
                return true;
            }
            case '[': return parseArray(out, depth);
            case '{': return parseObject(out, depth);
            default: return parseNumber(out);
        }
    }
    bool parseLiteral(const char* lit, Value& out, Value val) {
        for (const char* p = lit; *p; ++p) {
            if (cur >= end || *cur != *p) {
                error = std::string("invalid literal near '") + lit + "'";
                return false;
            }
            ++cur;
        }
        out = std::move(val);
        return true;
    }
    bool parseString(std::string& out) {
        ++cur;  // opening quote
        std::string s;
        while (cur < end && *cur != '"') {
            if (*cur == '\\') {
                ++cur;
                if (cur >= end) break;
                switch (*cur) {
                    case '"': s.push_back('"'); break;
                    case '\\': s.push_back('\\'); break;
                    case '/': s.push_back('/'); break;
                    case 'n': s.push_back('\n'); break;
                    case 'r': s.push_back('\r'); break;
                    case 't': s.push_back('\t'); break;
                    case 'b': s.push_back('\b'); break;
                    case 'f': s.push_back('\f'); break;
                    case 'u': {
                        // Minimal \uXXXX: BMP only, encoded as UTF-8.
                        if (end - cur < 5) {
                            error = "truncated \\u escape";
                            return false;
                        }
                        unsigned code = 0;
                        for (int i = 1; i <= 4; ++i) {
                            char c = cur[i];
                            code <<= 4;
                            if (c >= '0' && c <= '9') code |= static_cast<unsigned>(c - '0');
                            else if (c >= 'a' && c <= 'f')
                                code |= static_cast<unsigned>(c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F')
                                code |= static_cast<unsigned>(c - 'A' + 10);
                            else {
                                error = "invalid \\u escape";
                                return false;
                            }
                        }
                        cur += 5;
                        if (code < 0x80) {
                            s.push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            s.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            s.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            s.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            s.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            s.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        continue;
                    }
                    default:
                        error = "invalid escape";
                        return false;
                }
                ++cur;
            } else {
                s.push_back(*cur++);
            }
        }
        if (cur >= end) {
            error = "unterminated string";
            return false;
        }
        ++cur;  // closing quote
        out = std::move(s);
        return true;
    }
    bool parseNumber(Value& out) {
        const char* start = cur;
        if (cur < end && (*cur == '-' || *cur == '+')) ++cur;
        bool any = false;
        while (cur < end &&
               (std::isdigit(static_cast<unsigned char>(*cur)) || *cur == '.' || *cur == 'e' ||
                *cur == 'E' || *cur == '+' || *cur == '-')) {
            ++cur;
            any = true;
        }
        if (!any) {
            error = "invalid value";
            return false;
        }
        try {
            out = Value::makeNumber(std::stod(std::string(start, cur)));
        } catch (...) {
            error = "invalid number";
            return false;
        }
        return true;
    }
    bool parseArray(Value& out, int depth) {
        ++cur;
        Array arr;
        skipWs();
        if (cur < end && *cur == ']') {
            ++cur;
            out = Value::makeArray(std::move(arr));
            return true;
        }
        while (true) {
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            arr.push_back(std::move(v));
            skipWs();
            if (cur >= end) {
                error = "unterminated array";
                return false;
            }
            if (*cur == ',') {
                ++cur;
                continue;
            }
            if (*cur == ']') {
                ++cur;
                out = Value::makeArray(std::move(arr));
                return true;
            }
            error = "expected ',' or ']' in array";
            return false;
        }
    }
    bool parseObject(Value& out, int depth) {
        ++cur;
        Object obj;
        skipWs();
        if (cur < end && *cur == '}') {
            ++cur;
            out = Value::makeObject(std::move(obj));
            return true;
        }
        while (true) {
            skipWs();
            if (cur >= end || *cur != '"') {
                error = "expected string key in object";
                return false;
            }
            std::string key;
            if (!parseString(key)) return false;
            if (!consume(':')) {
                error = "expected ':' in object";
                return false;
            }
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            obj[std::move(key)] = std::move(v);
            skipWs();
            if (cur >= end) {
                error = "unterminated object";
                return false;
            }
            if (*cur == ',') {
                ++cur;
                continue;
            }
            if (*cur == '}') {
                ++cur;
                out = Value::makeObject(std::move(obj));
                return true;
            }
            error = "expected ',' or '}' in object";
            return false;
        }
    }
};

}  // namespace

Result<Value> parse(const std::string& text, const std::string& asset) {
    Parser p{text.data(), text.data() + text.size(), asset, {}};
    Value v;
    if (!p.parseValue(v, 0))
        return Result<Value>::fail(p.error.empty() ? "invalid JSON" : p.error, "FORMAT", asset,
                                   "json.parse");
    p.skipWs();
    if (p.cur != p.end)
        return Result<Value>::fail("trailing characters after JSON document.", "FORMAT", asset,
                                   "json.parse");
    return Result<Value>::ok(std::move(v));
}

std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

namespace {
void writeValue(const Value& v, std::string& out, int indent, bool pretty) {
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    const std::string padChild(static_cast<std::size_t>(indent + 1) * 2, ' ');
    switch (v.type) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.boolean ? "true" : "false"; break;
        case Value::Type::Number: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.17g", v.number);
            out += buf;
            break;
        }
        case Value::Type::String: out += "\"" + escapeString(v.str) + "\""; break;
        case Value::Type::Array: {
            const Array& a = v.asArray();
            if (a.empty()) {
                out += "[]";
                break;
            }
            out += "[";
            if (pretty) out += "\n";
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (pretty) out += padChild;
                writeValue(a[i], out, indent + 1, pretty);
                if (i + 1 < a.size()) out += ",";
                if (pretty) out += "\n";
            }
            if (pretty) out += pad;
            out += "]";
            break;
        }
        case Value::Type::Object: {
            const Object& o = v.asObject();
            if (o.empty()) {
                out += "{}";
                break;
            }
            out += "{";
            if (pretty) out += "\n";
            std::size_t i = 0;
            for (const auto& kv : o) {
                if (pretty) out += padChild;
                out += "\"" + escapeString(kv.first) + "\":";
                if (pretty) out += " ";
                writeValue(kv.second, out, indent + 1, pretty);
                if (++i < o.size()) out += ",";
                if (pretty) out += "\n";
            }
            if (pretty) out += pad;
            out += "}";
            break;
        }
    }
}
}  // namespace

std::string serialize(const Value& value, bool pretty) {
    std::string out;
    writeValue(value, out, 0, pretty);
    if (pretty) out += "\n";
    return out;
}

}  // namespace m2rig::json
