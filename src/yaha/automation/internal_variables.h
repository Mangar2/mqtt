#pragma once

#include <chrono>
#include <map>
#include <string>
#include <variant>

namespace yaha {

/**
 * @brief Calculates the built-in automation variables for one evaluation date.
 *
 * Internal variables contain current time, weekday and all sun/twilight events
 * derived from date plus geo coordinates.
 */
class InternalVariables {
public:
    using TimePoint = std::chrono::system_clock::time_point;
    using Value = std::variant<TimePoint, double>;
    using VariableMap = std::map<std::string, Value>;

    /**
     * @brief Creates calculator with fixed geo coordinates.
     *
     * @param longitude Longitude in decimal degrees.
     * @param latitude Latitude in decimal degrees.
     */
    InternalVariables(double longitude, double latitude);

    /**
     * @brief Computes all internal variables for the given date context.
     *
     * Produced keys:
     * `/time`, `/weekday`, `/sunrise`, `/sunset`, `/civildawn`, `/civildusk`,
     * `/nauticaldawn`, `/nauticaldusk`, `/astronomicaldawn`, `/astronomicaldusk`.
     *
     * @param date Evaluation date/time.
     * @return Fully filled internal variable map.
     * @throws std::runtime_error If a sun event is undefined for given date/geo.
     */
    [[nodiscard]] VariableMap calculate(const TimePoint& date) const;

private:
    [[nodiscard]] static TimePoint calculateSunEvent(
        const TimePoint& date,
        double longitude,
        double latitude,
        double zenithDegrees,
        bool sunriseEvent);

    [[nodiscard]] static double weekdayIndex(const TimePoint& date);

    double longitude_;
    double latitude_;
};

} // namespace yaha
