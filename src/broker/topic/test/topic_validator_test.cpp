#include <catch2/catch_test_macros.hpp>

#include <string>

#include "topic/topic_error.h"
#include "topic/topic_validator.h"

using namespace mqtt;

// ---------------------------------------------------------------------------
// validate_topic_name
// ---------------------------------------------------------------------------

TEST_CASE("topic_name_valid_simple", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_name("sport/tennis/player1"));
}

TEST_CASE("topic_name_valid_single_char", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_name("a"));
}

TEST_CASE("topic_name_valid_single_slash", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_name("/"));
}

TEST_CASE("topic_name_valid_max_length", "[topic_validator]") {
  const std::string max_topic(65535U, 'a');
  CHECK_NOTHROW(validate_topic_name(max_topic));
}

TEST_CASE("topic_name_empty", "[topic_validator]") {
  try {
    validate_topic_name("");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::EmptyTopic);
  }
}

TEST_CASE("topic_name_too_long", "[topic_validator]") {
  const std::string long_topic(65536U, 'a');
  try {
    validate_topic_name(long_topic);
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::TopicTooLong);
  }
}

TEST_CASE("topic_name_null_char", "[topic_validator]") {
  const std::string null_topic{"sport\0tennis", 12};
  try {
    validate_topic_name(null_topic);
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::NullCharacter);
  }
}

TEST_CASE("topic_name_wildcard_hash", "[topic_validator]") {
  try {
    validate_topic_name("sport/#");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::WildcardInTopicName);
  }
}

TEST_CASE("topic_name_wildcard_plus", "[topic_validator]") {
  try {
    validate_topic_name("sport/+/player");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::WildcardInTopicName);
  }
}

TEST_CASE("topic_name_hash_only", "[topic_validator]") {
  try {
    validate_topic_name("#");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::WildcardInTopicName);
  }
}

TEST_CASE("topic_name_plus_only", "[topic_validator]") {
  try {
    validate_topic_name("+");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::WildcardInTopicName);
  }
}

TEST_CASE("topic_name_system_topic", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_name("$SYS/info"));
}

// ---------------------------------------------------------------------------
// validate_topic_filter
// ---------------------------------------------------------------------------

TEST_CASE("topic_filter_valid_no_wildcards", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("sport/tennis"));
}

TEST_CASE("topic_filter_valid_hash_only", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("#"));
}

TEST_CASE("topic_filter_valid_hash_at_end", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("sport/#"));
}

TEST_CASE("topic_filter_valid_plus_single_level", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("+"));
}

TEST_CASE("topic_filter_valid_plus_start", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("+/tennis"));
}

TEST_CASE("topic_filter_valid_plus_end", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("sport/+"));
}

TEST_CASE("topic_filter_valid_plus_middle", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("sport/+/player"));
}

TEST_CASE("topic_filter_valid_multiple_plus", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("+/+/+"));
}

TEST_CASE("topic_filter_valid_plus_and_hash", "[topic_validator]") {
  CHECK_NOTHROW(validate_topic_filter("sport/+/#"));
}

TEST_CASE("topic_filter_valid_max_length", "[topic_validator]") {
  const std::string max_filter(65535U, 'a');
  CHECK_NOTHROW(validate_topic_filter(max_filter));
}

TEST_CASE("topic_filter_empty", "[topic_validator]") {
  try {
    validate_topic_filter("");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::EmptyTopic);
  }
}

TEST_CASE("topic_filter_too_long", "[topic_validator]") {
  const std::string long_filter(65536U, 'a');
  try {
    validate_topic_filter(long_filter);
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::TopicTooLong);
  }
}

TEST_CASE("topic_filter_null_char", "[topic_validator]") {
  const std::string null_filter{"sport\0tennis", 12};
  try {
    validate_topic_filter(null_filter);
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::NullCharacter);
  }
}

TEST_CASE("topic_filter_hash_not_last", "[topic_validator]") {
  try {
    validate_topic_filter("sport/#/player");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::InvalidWildcard);
  }
}

TEST_CASE("topic_filter_hash_without_sep", "[topic_validator]") {
  try {
    validate_topic_filter("sport#");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::InvalidWildcard);
  }
}

TEST_CASE("topic_filter_plus_not_full_level_start", "[topic_validator]") {
  try {
    validate_topic_filter("sport+/player");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::InvalidWildcard);
  }
}

TEST_CASE("topic_filter_plus_not_full_level_end", "[topic_validator]") {
  try {
    validate_topic_filter("sport/play+er");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::InvalidWildcard);
  }
}

TEST_CASE("topic_filter_plus_embedded", "[topic_validator]") {
  try {
    validate_topic_filter("sp+rt");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::InvalidWildcard);
  }
}

TEST_CASE("topic_filter_hash_middle_no_sep", "[topic_validator]") {
  try {
    validate_topic_filter("sport#player");
    FAIL("expected TopicException");
  } catch (const TopicException &exc) {
    CHECK(exc.error() == TopicError::InvalidWildcard);
  }
}

// ---------------------------------------------------------------------------
// is_system_topic
// ---------------------------------------------------------------------------

TEST_CASE("system_topic_dollar_prefix", "[topic_validator]") {
  CHECK(is_system_topic("$SYS/info") == true);
}

TEST_CASE("system_topic_dollar_only", "[topic_validator]") {
  CHECK(is_system_topic("$") == true);
}

TEST_CASE("system_topic_normal", "[topic_validator]") {
  CHECK(is_system_topic("sport/tennis") == false);
}

TEST_CASE("system_topic_empty", "[topic_validator]") {
  CHECK(is_system_topic("") == false);
}

TEST_CASE("system_topic_slash_prefix", "[topic_validator]") {
  CHECK(is_system_topic("/sport") == false);
}
