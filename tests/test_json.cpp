// JSON parser/serializer tests.
#include <cstdio>

#include "../tests/expect.hpp"
#include "m2rig/json.hpp"

using namespace m2rig;

M2RIG_TEST(json, roundtrip_types) {
    int failures = 0;
    const char* text = R"({"name":"armor","count":3,"ok":true,"list":[1,2,3],"obj":{"a":null}})";
    auto res = json::parse(text, "t");
    CHECK_TRUE(res.succeeded());
    if (!res.succeeded()) return failures + 1;
    const json::Value& v = res.value();
    CHECK_TRUE(v.getString("name") == "armor");
    CHECK_NEAR(v.getNumber("count"), 3.0, 1e-9);
    CHECK_TRUE(v.getBool("ok"));
    const json::Value* list = v.find("list");
    CHECK_TRUE(list && list->type == json::Value::Type::Array && list->asArray().size() == 3u);
    const std::string back = json::serialize(v, false);
    auto res2 = json::parse(back, "t2");
    CHECK_TRUE(res2.succeeded());
    CHECK_TRUE(res2.succeeded() && res2.value().getString("name") == "armor");
    return failures;
}

M2RIG_TEST(json, rejects_malformed) {
    int failures = 0;
    CHECK_FALSE(json::parse("{bad", "t").succeeded());
    CHECK_FALSE(json::parse("[1,]", "t").succeeded());
    CHECK_FALSE(json::parse("{\"a\":1} trailing", "t").succeeded());
    CHECK_TRUE(json::parse("  { }  ", "t").succeeded());
    return failures;
}
