#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "yaha/automation/expression_tokenizer.h"

TEST_CASE("expression_tokenizer_splits_declaration_and_expression", "[yaha][automation]") {
    const std::string program =
        "presence=(1:awake,default:absent)\n"
        "$MONITOR/presence!=presence($MONITOR/presence/set)";

    const std::vector<std::string> expected = {
        "presence", "=", "(", "1", ":", "awake", ",", "default", ":", "absent", ")",
        "\\n",
        "$MONITOR/presence", "!=", "presence", "(", "$MONITOR/presence/set", ")"
    };

    REQUIRE(yaha::ExpressionTokenizer::tokenize(program) == expected);
}

TEST_CASE("expression_tokenizer_keeps_quoted_literals_as_single_token", "[yaha][automation]") {
    const std::string program = "if('sleep mode' = mode, \"on\", off)";

    const std::vector<std::string> expected = {
        "if", "(", "'sleep mode'", "=", "mode", ",", "\"on\"", ",", "off", ")"
    };

    REQUIRE(yaha::ExpressionTokenizer::tokenize(program) == expected);
}

TEST_CASE("expression_tokenizer_recognizes_comparators", "[yaha][automation]") {
    const std::string program = "a=b and c!=d and e<>f and g>=h and i<=j and k>l and m<n";

    const std::vector<std::string> expected = {
        "a", "=", "b", "and", "c", "!=", "d", "and", "e", "<>", "f", "and",
        "g", ">=", "h", "and", "i", "<=", "j", "and", "k", ">", "l", "and", "m", "<", "n"
    };

    REQUIRE(yaha::ExpressionTokenizer::tokenize(program) == expected);
}

TEST_CASE("expression_tokenizer_keeps_variable_reference_with_spaces", "[yaha][automation]") {
    const std::string program = "$MONITOR/room/motion sensor/detection state = on";

    const std::vector<std::string> expected = {
        "$MONITOR/room/motion sensor/detection state", "=", "on"
    };

    REQUIRE(yaha::ExpressionTokenizer::tokenize(program) == expected);
}

TEST_CASE("expression_tokenizer_normalizes_crlf_to_newline_token", "[yaha][automation]") {
    const std::string program = "a=b\r\nc=d";

    const std::vector<std::string> expected = {
        "a", "=", "b", "\\n", "c", "=", "d"
    };

    REQUIRE(yaha::ExpressionTokenizer::tokenize(program) == expected);
}

TEST_CASE("expression_tokenizer_rejects_unterminated_quote", "[yaha][automation]") {
    REQUIRE_THROWS_AS(yaha::ExpressionTokenizer::tokenize("'abc"), std::invalid_argument);
}

TEST_CASE("expression_tokenizer_rejects_lone_exclamation_mark", "[yaha][automation]") {
    REQUIRE_THROWS_AS(yaha::ExpressionTokenizer::tokenize("a ! b"), std::invalid_argument);
}

TEST_CASE("expression_tokenizer_keeps_negative_number_literal", "[yaha][automation]") {
    const std::string program = "if(a = b, -12.5, 3)";

    const std::vector<std::string> expected = {
        "if", "(", "a", "=", "b", ",", "-12.5", ",", "3", ")"
    };

    REQUIRE(yaha::ExpressionTokenizer::tokenize(program) == expected);
}
