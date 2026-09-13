#pragma once
// Minimal dependency-free JSON value, parser and serializer.
// Covers exactly what .m2rig projects, manifests and reports need:
// null/bool/number/string/array/object, UTF-8 passthrough, full double
// precision. Malformed input returns Result, never throws outwards.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "m2rig/result.hpp"

namespace m2rig::json {

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value {
    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::shared_ptr<Array> arr;
    std::shared_ptr<Object> obj;

    static Value makeNull() { return Value{}; }
    static Value makeBool(bool b) {
        Value v;
        v.type = Type::Bool;
        v.boolean = b;
        return v;
    }
    static Value makeNumber(double n) {
        Value v;
        v.type = Type::Number;
        v.number = n;
        return v;
    }
    static Value makeString(std::string s) {
        Value v;
        v.type = Type::String;
        v.str = std::move(s);
        return v;
    }
    static Value makeArray(Array a) {
        Value v;
        v.type = Type::Array;
        v.arr = std::make_shared<Array>(std::move(a));
        return v;
    }
    static Value makeObject(Object o) {
        Value v;
        v.type = Type::Object;
        v.obj = std::make_shared<Object>(std::move(o));
        return v;
    }

    bool isNull() const { return type == Type::Null; }
    const Array& asArray() const { return *arr; }
    const Object& asObject() const { return *obj; }
    const Value* find(const std::string& key) const;
    std::string getString(const std::string& key, const std::string& fallback = {}) const;
    double getNumber(const std::string& key, double fallback = 0.0) const;
    bool getBool(const std::string& key, bool fallback = false) const;
};

Result<Value> parse(const std::string& text, const std::string& asset = "<json>");
std::string serialize(const Value& value, bool pretty = true);
std::string escapeString(const std::string& s);

}  // namespace m2rig::json
