#include "yaha/automation/internal_variables.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace yaha {
namespace {

constexpr double k_degrees_full_turn{360.0};
constexpr double k_degrees_half_turn{180.0};
constexpr double k_hours_per_day{24.0};
constexpr double k_degrees_per_hour{15.0};

constexpr double k_solar_zenith_official{90.833};
constexpr double k_solar_zenith_civil{96.0};
constexpr double k_solar_zenith_nautical{102.0};
constexpr double k_solar_zenith_astronomical{108.0};

constexpr double k_right_ascension_factor{0.91764};

[[nodiscard]] double degreesToRadians(const double degrees) {
    return degrees * (std::numbers::pi_v<double> / k_degrees_half_turn);
}

[[nodiscard]] double radiansToDegrees(const double radians) {
    return radians * (k_degrees_half_turn / std::numbers::pi_v<double>);
}

[[nodiscard]] double normalizeAngle(const double degrees) {
    double normalized = std::fmod(degrees, k_degrees_full_turn);
    if (normalized < 0.0) {
        normalized += k_degrees_full_turn;
    }
    return normalized;
}

[[nodiscard]] double normalizeHours(const double hours) {
    double normalized = std::fmod(hours, k_hours_per_day);
    if (normalized < 0.0) {
        normalized += k_hours_per_day;
    }
    return normalized;
}

[[nodiscard]] std::chrono::sys_days toUtcDay(const InternalVariables::TimePoint& date) {
    return std::chrono::floor<std::chrono::days>(date);
}

[[nodiscard]] int dayOfYear(const InternalVariables::TimePoint& date) {
    const std::chrono::sys_days day = toUtcDay(date);
    const std::chrono::year_month_day ymd{day};
    const std::chrono::sys_days yearStart{ymd.year() / std::chrono::January / 1};
    return static_cast<int>((day - yearStart).count()) + 1;
}

[[nodiscard]] InternalVariables::TimePoint makeUtcTimePoint(
    const InternalVariables::TimePoint& date,
    const double utcHours) {
    const std::chrono::sys_days day = toUtcDay(date);
    const auto offset = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::duration<double>(utcHours * 3600.0));
    return day + offset;
}

} // namespace

InternalVariables::InternalVariables(const GeoCoordinates coordinates)
    : coordinates_(coordinates) {
}

InternalVariables::VariableMap InternalVariables::calculate(const TimePoint& date) const {
    VariableMap values;

    values["/time"] = date;
    values["/weekday"] = weekdayIndex(date);

    values["/sunrise"] = calculateSunEvent(date, coordinates_, k_solar_zenith_official, true);
    values["/civildawn"] = calculateSunEvent(date, coordinates_, k_solar_zenith_civil, true);
    values["/nauticaldawn"] = calculateSunEvent(date, coordinates_, k_solar_zenith_nautical, true);
    values["/astronomicaldawn"] = calculateSunEvent(date, coordinates_, k_solar_zenith_astronomical, true);

    values["/sunset"] = calculateSunEvent(date, coordinates_, k_solar_zenith_official, false);
    values["/civildusk"] = calculateSunEvent(date, coordinates_, k_solar_zenith_civil, false);
    values["/nauticaldusk"] = calculateSunEvent(date, coordinates_, k_solar_zenith_nautical, false);
    values["/astronomicaldusk"] = calculateSunEvent(date, coordinates_, k_solar_zenith_astronomical, false);

    return values;
}

InternalVariables::TimePoint InternalVariables::calculateSunEvent(
    const TimePoint& date,
    const GeoCoordinates& coordinates,
    const double zenithDegrees,
    const bool sunriseEvent) {
    const double lngHour = coordinates.longitude / k_degrees_per_hour;
    const int dayIndex = dayOfYear(date);
    const double approximateTime = sunriseEvent
        ? static_cast<double>(dayIndex) + ((6.0 - lngHour) / 24.0)
        : static_cast<double>(dayIndex) + ((18.0 - lngHour) / 24.0);

    const double meanAnomaly = (0.9856 * approximateTime) - 3.289;

    const double trueLongitude = normalizeAngle(
        meanAnomaly
        + (1.916 * std::sin(degreesToRadians(meanAnomaly)))
        + (0.020 * std::sin(degreesToRadians(2.0 * meanAnomaly)))
        + 282.634);

    double rightAscension = normalizeAngle(
        radiansToDegrees(std::atan(k_right_ascension_factor * std::tan(degreesToRadians(trueLongitude)))));

    const double longitudeQuadrant = std::floor(trueLongitude / 90.0) * 90.0;
    const double rightAscensionQuadrant = std::floor(rightAscension / 90.0) * 90.0;
    rightAscension += longitudeQuadrant - rightAscensionQuadrant;
    rightAscension /= k_degrees_per_hour;

    const double sinDeclination = 0.39782 * std::sin(degreesToRadians(trueLongitude));
    const double cosDeclination = std::cos(std::asin(sinDeclination));

    const double cosHourAngle = (
        std::cos(degreesToRadians(zenithDegrees))
        - (sinDeclination * std::sin(degreesToRadians(coordinates.latitude))))
        / (cosDeclination * std::cos(degreesToRadians(coordinates.latitude)));

    if (cosHourAngle > 1.0 || cosHourAngle < -1.0) {
        throw std::runtime_error("sun event is undefined for date and coordinates");
    }

    double hourAngle = sunriseEvent
        ? k_degrees_full_turn - radiansToDegrees(std::acos(cosHourAngle))
        : radiansToDegrees(std::acos(cosHourAngle));
    hourAngle /= k_degrees_per_hour;

    const double localMeanTime = hourAngle + rightAscension - (0.06571 * approximateTime) - 6.622;
    const double utcHours = normalizeHours(localMeanTime - lngHour);

    return makeUtcTimePoint(date, utcHours);
}

double InternalVariables::weekdayIndex(const TimePoint& date) {
    const std::chrono::weekday weekday{toUtcDay(date)};
    return static_cast<double>(weekday.c_encoding());
}

} // namespace yaha
