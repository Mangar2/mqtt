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

    struct GeoCoordinates {
        double longitude;
        double latitude;
    };

    /**
     * @brief Creates calculator with fixed geo coordinates.
     *
    * @param coordinates Longitude and latitude in decimal degrees.
     */
    InternalVariables(GeoCoordinates coordinates);

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
        const GeoCoordinates& coordinates,
        double zenithDegrees,
        bool sunriseEvent);

    [[nodiscard]] static double weekdayIndex(const TimePoint& date);

    GeoCoordinates coordinates_;
};

} // namespace yaha
