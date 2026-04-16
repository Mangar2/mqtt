#include <catch2/catch_test_macros.hpp>

#include "data_model/types/variable_byte_integer.h"
#include "data_model/types/utf8_string.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/integers.h"
#include "data_model/types/qos.h"

using namespace mqtt;

// ── VariableByteInteger ────────────────────────────────────────────────────────

TEST_CASE("vbi_default_value", "[types][vbi]")
{
    constexpr VariableByteInteger vbi{};
    STATIC_CHECK(vbi.value == 0U);
}

TEST_CASE("vbi_encoded_size_1byte", "[types][vbi]")
{
    CHECK(VariableByteInteger{0}.encoded_size()   == 1);
    CHECK(VariableByteInteger{127}.encoded_size() == 1);
}

TEST_CASE("vbi_encoded_size_2byte", "[types][vbi]")
{
    CHECK(VariableByteInteger{128}.encoded_size()   == 2);
    CHECK(VariableByteInteger{16383}.encoded_size() == 2);
}

TEST_CASE("vbi_encoded_size_3byte", "[types][vbi]")
{
    CHECK(VariableByteInteger{16384}.encoded_size()   == 3);
    CHECK(VariableByteInteger{2097151}.encoded_size() == 3);
}

TEST_CASE("vbi_encoded_size_4byte", "[types][vbi]")
{
    CHECK(VariableByteInteger{2097152}.encoded_size()   == 4);
    CHECK(VariableByteInteger{268435455}.encoded_size() == 4);
}

TEST_CASE("vbi_max_value_constant", "[types][vbi]")
{
    STATIC_CHECK(VariableByteInteger::k_max_value == 268'435'455U);
}

TEST_CASE("vbi_equality", "[types][vbi]")
{
    constexpr VariableByteInteger a{42};
    constexpr VariableByteInteger b{42};
    constexpr VariableByteInteger c{99};
    STATIC_CHECK(a == b);
    STATIC_CHECK(a != c);
}

// ── Utf8String ─────────────────────────────────────────────────────────────────

TEST_CASE("utf8_default_empty", "[types][utf8]")
{
    Utf8String s{};
    CHECK(s.value.empty());
}

TEST_CASE("utf8_max_byte_length", "[types][utf8]")
{
    STATIC_CHECK(Utf8String::k_max_byte_length == 65535U);
}

TEST_CASE("utf8_equality", "[types][utf8]")
{
    Utf8String a{"hello"};
    Utf8String b{"hello"};
    Utf8String c{"world"};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("utf8_pair_equality", "[types][utf8]")
{
    Utf8StringPair p1{Utf8String{"k"}, Utf8String{"v"}};
    Utf8StringPair p2{Utf8String{"k"}, Utf8String{"v"}};
    Utf8StringPair p3{Utf8String{"k"}, Utf8String{"x"}};
    CHECK(p1 == p2);
    CHECK(p1 != p3);
}

// ── BinaryData ─────────────────────────────────────────────────────────────────

TEST_CASE("binary_default_empty", "[types][binary]")
{
    BinaryData b{};
    CHECK(b.data.empty());
}

TEST_CASE("binary_max_byte_length", "[types][binary]")
{
    STATIC_CHECK(BinaryData::k_max_byte_length == 65535U);
}

TEST_CASE("binary_equality", "[types][binary]")
{
    BinaryData a{{0x01, 0x02}};
    BinaryData b{{0x01, 0x02}};
    BinaryData c{{0x03}};
    CHECK(a == b);
    CHECK(a != c);
}

// ── Integers ───────────────────────────────────────────────────────────────────

TEST_CASE("two_byte_size", "[types][integers]")
{
    STATIC_CHECK(sizeof(TwoByteInteger) == 2U);
}

TEST_CASE("four_byte_size", "[types][integers]")
{
    STATIC_CHECK(sizeof(FourByteInteger) == 4U);
}

// ── QoS ───────────────────────────────────────────────────────────────────────

TEST_CASE("qos_values", "[types][qos]")
{
    using U = std::underlying_type_t<QoS>;
    STATIC_CHECK(static_cast<U>(QoS::AtMostOnce)  == 0);
    STATIC_CHECK(static_cast<U>(QoS::AtLeastOnce) == 1);
    STATIC_CHECK(static_cast<U>(QoS::ExactlyOnce) == 2);
}
