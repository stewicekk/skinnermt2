#pragma once
// Explicit error model. No exceptions cross module boundaries; malformed
// assets always become an explicit Err value (spec sections 44-45).
#include <string>
#include <utility>
#include <vector>

namespace m2rig {

struct Err {
    std::string message;
    std::string category;  // e.g. "FORMAT", "IO", "VALIDATION"
    std::string asset;
    std::string operation;
};

template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value), {}, true); }
    static Result fail(Err err) { return Result(T{}, std::move(err), false); }
    static Result fail(std::string message, std::string category = "GENERAL",
                       std::string asset = {}, std::string operation = {}) {
        return Result(T{}, Err{std::move(message), std::move(category), std::move(asset),
                               std::move(operation)},
                      false);
    }

    bool ok() const = delete;  // use succeeded(); 'ok' is the factory below
    bool succeeded() const { return ok_; }
    explicit operator bool() const { return ok_; }
    T& value() { return value_; }
    const T& value() const { return value_; }
    Err& error() { return error_; }
    const Err& error() const { return error_; }

private:
    Result(T v, Err e, bool ok) : value_(std::move(v)), error_(std::move(e)), ok_(ok) {}
    T value_;
    Err error_;
    bool ok_;
};

template <>
class Result<void> {
public:
    static Result ok() { return Result(true, {}); }
    static Result fail(Err err) { return Result(false, std::move(err)); }
    static Result fail(std::string message, std::string category = "GENERAL",
                       std::string asset = {}, std::string operation = {}) {
        return Result(false, Err{std::move(message), std::move(category), std::move(asset),
                                 std::move(operation)});
    }
    bool succeeded() const { return ok_; }
    explicit operator bool() const { return ok_; }
    Err& error() { return error_; }
    const Err& error() const { return error_; }

private:
    Result(bool ok, Err e) : ok_(ok), error_(std::move(e)) {}
    bool ok_;
    Err error_;
};

using ResultVoid = Result<void>;

inline Err makeErr(std::string message, std::string category = "GENERAL",
                   std::string asset = {}, std::string operation = {}) {
    return Err{std::move(message), std::move(category), std::move(asset), std::move(operation)};
}

}  // namespace m2rig
