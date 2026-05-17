#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_state/rs485_state.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct StateParityEvent {
    bool noMessage{false};
    std::uint8_t request{0U};
    bool notForMe{false};
};

struct StateParityExpected {
    std::uint8_t result{0U};
    std::uint8_t state{0U};
    std::uint32_t timer{0U};
    bool maySend{false};
    int rightSibling{-1};
    int leftmostSibling{-1};
    bool tokenLost{false};
};

struct StateParityStep {
    StateParityEvent event{};
    StateParityExpected expected{};
};

struct StateParityCase {
    std::string name{};
    std::vector<StateParityStep> steps{};
};

struct StateParitySnapshot {
    std::uint8_t result{0U};
    std::uint8_t state{0U};
    std::uint32_t timer{0U};
    bool maySend{false};
    int rightSibling{-1};
    int leftmostSibling{-1};
    bool tokenLost{false};

    [[nodiscard]] bool operator==(const StateParitySnapshot& other) const noexcept {
        return result == other.result &&
            state == other.state &&
            timer == other.timer &&
            maySend == other.maySend &&
            rightSibling == other.rightSibling &&
            leftmostSibling == other.leftmostSibling &&
            tokenLost == other.tokenLost;
    }
};

enum class FixtureRowType : std::uint8_t {
    Case,
    Step,
    End,
    Unknown,
};

namespace fixture_columns {
constexpr std::size_t rowType{0U};
constexpr std::size_t caseName{1U};
constexpr std::size_t noMessage{1U};
constexpr std::size_t request{2U};
constexpr std::size_t notForMe{3U};
constexpr std::size_t result{4U};
constexpr std::size_t state{5U};
constexpr std::size_t timer{6U};
constexpr std::size_t maySend{7U};
constexpr std::size_t rightSibling{8U};
constexpr std::size_t leftmostSibling{9U};
constexpr std::size_t tokenLost{10U};
constexpr std::size_t stepColumnCount{11U};
constexpr std::size_t caseColumnCount{2U};
constexpr std::size_t endColumnCount{1U};
} // namespace fixture_columns

[[nodiscard]] std::vector<std::string> splitByPipe(const std::string& text) {
    std::vector<std::string> tokens{};
    std::stringstream stream{text};
    std::string token{};
    while (std::getline(stream, token, '|')) {
        tokens.push_back(token);
    }
    return tokens;
}

[[nodiscard]] int toInt(const std::string& text) {
    return std::stoi(text);
}

[[nodiscard]] bool toBool(const std::string& text) {
    return text == "1";
}

[[nodiscard]] FixtureRowType detectRowType(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        return FixtureRowType::Unknown;
    }

    if (tokens[fixture_columns::rowType] == "CASE") {
        return FixtureRowType::Case;
    }

    if (tokens[fixture_columns::rowType] == "STEP") {
        return FixtureRowType::Step;
    }

    if (tokens[fixture_columns::rowType] == "END") {
        return FixtureRowType::End;
    }

    return FixtureRowType::Unknown;
}

void appendStep(StateParityCase& parityCase, const std::vector<std::string>& tokens) {
    REQUIRE(tokens.size() == fixture_columns::stepColumnCount);

    StateParityStep step{};
    step.event.noMessage = toBool(tokens[fixture_columns::noMessage]);
    step.event.request = static_cast<std::uint8_t>(toInt(tokens[fixture_columns::request]));
    step.event.notForMe = toBool(tokens[fixture_columns::notForMe]);

    step.expected.result = static_cast<std::uint8_t>(toInt(tokens[fixture_columns::result]));
    step.expected.state = static_cast<std::uint8_t>(toInt(tokens[fixture_columns::state]));
    step.expected.timer = static_cast<std::uint32_t>(toInt(tokens[fixture_columns::timer]));
    step.expected.maySend = toBool(tokens[fixture_columns::maySend]);
    step.expected.rightSibling = toInt(tokens[fixture_columns::rightSibling]);
    step.expected.leftmostSibling = toInt(tokens[fixture_columns::leftmostSibling]);
    step.expected.tokenLost = toBool(tokens[fixture_columns::tokenLost]);

    parityCase.steps.push_back(step);
}

void startCase(
    StateParityCase& current,
    bool& inCase,
    const std::vector<std::string>& tokens) {
    REQUIRE(tokens.size() == fixture_columns::caseColumnCount);
    REQUIRE(!inCase);
    current = StateParityCase{};
    current.name = tokens[fixture_columns::caseName];
    inCase = true;
}

void finishCase(
    std::vector<StateParityCase>& cases,
    StateParityCase& current,
    bool& inCase,
    const std::vector<std::string>& tokens) {
    REQUIRE(tokens.size() == fixture_columns::endColumnCount);
    REQUIRE(inCase);
    cases.push_back(std::move(current));
    current = StateParityCase{};
    inCase = false;
}

void processFixtureLine(
    std::vector<StateParityCase>& cases,
    StateParityCase& current,
    bool& inCase,
    const std::string& line) {
    if (line.empty()) {
        return;
    }

    const auto tokens = splitByPipe(line);
    const FixtureRowType rowType = detectRowType(tokens);
    switch (rowType) {
    case FixtureRowType::Case:
        startCase(current, inCase, tokens);
        return;
    case FixtureRowType::Step:
        REQUIRE(inCase);
        appendStep(current, tokens);
        return;
    case FixtureRowType::End:
        finishCase(cases, current, inCase, tokens);
        return;
    case FixtureRowType::Unknown:
        FAIL("Unknown fixture row type");
    }
}

[[nodiscard]] std::vector<StateParityCase> loadFixture() {
    const auto fixturePath = std::filesystem::path{__FILE__}.parent_path() / "rs485_state_parity_fixture.txt";
    std::ifstream fixtureStream{fixturePath};
    REQUIRE(fixtureStream.good());

    std::vector<StateParityCase> cases{};
    StateParityCase current{};
    bool inCase = false;

    std::string line{};
    while (std::getline(fixtureStream, line)) {
        processFixtureLine(cases, current, inCase, line);
    }

    REQUIRE(!inCase);
    return cases;
}

[[nodiscard]] int siblingOrMinusOne(const std::optional<std::uint8_t> sibling) {
    if (!sibling.has_value()) {
        return -1;
    }

    return static_cast<int>(*sibling);
}

[[nodiscard]] yaha::Rs485StateResult applyParityEvent(
    yaha::Rs485State& state,
    const StateParityEvent& event) {
    if (event.noMessage) {
        return state.updateStateNoMessage();
    }

    return state.updateState(event.request, event.notForMe);
}

void assertStateMatchesExpected(
    const yaha::Rs485State& state,
    const yaha::Rs485StateResult result,
    const StateParityExpected& expected) {
    const StateParitySnapshot actualSnapshot{
        .result = static_cast<std::uint8_t>(result),
        .state = static_cast<std::uint8_t>(state.state()),
        .timer = state.timer(),
        .maySend = state.maySend(),
        .rightSibling = siblingOrMinusOne(state.rightSibling()),
        .leftmostSibling = siblingOrMinusOne(state.leftmostSibling()),
        .tokenLost = state.tokenLost()};

    const StateParitySnapshot expectedSnapshot{
        .result = expected.result,
        .state = expected.state,
        .timer = expected.timer,
        .maySend = expected.maySend,
        .rightSibling = expected.rightSibling,
        .leftmostSibling = expected.leftmostSibling,
        .tokenLost = expected.tokenLost};

    REQUIRE(actualSnapshot == expectedSnapshot);
}

void runParityCase(const StateParityCase& parityCase) {
    yaha::Rs485State state{};

    for (std::size_t stepIndex = 0U; stepIndex < parityCase.steps.size(); ++stepIndex) {
        const auto& step = parityCase.steps[stepIndex];
        const yaha::Rs485StateResult result = applyParityEvent(state, step.event);

        INFO("case=" << parityCase.name << " step=" << stepIndex);
        assertStateMatchesExpected(state, result, step.expected);
    }
}

} // namespace

TEST_CASE("rs485_state_transition_matrix_matches_legacy_oracle_fixture", "[rs485_state][parity]") {
    const auto parityCases = loadFixture();

    for (const auto& parityCase : parityCases) {
        if (!parityCase.name.starts_with("matrix_")) {
            continue;
        }

        runParityCase(parityCase);
    }
}

TEST_CASE("rs485_state_long_replay_matches_legacy_oracle_fixture", "[rs485_state][parity]") {
    const auto parityCases = loadFixture();

    for (const auto& parityCase : parityCases) {
        if (!parityCase.name.starts_with("long_replay")) {
            continue;
        }

        runParityCase(parityCase);
    }
}
