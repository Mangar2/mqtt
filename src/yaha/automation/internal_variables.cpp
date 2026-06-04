#include "yaha/automation/internal_variables.h"

#include <cmath>
#include <ctime>
#include <iomanip>
#include <numbers>
#include <sstream>
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
constexpr double k_cos_hour_angle_min{-1.0};
constexpr double k_cos_hour_angle_max{1.0};
constexpr int k_tm_year_offset{1900};

[[nodiscard]] std::string toUtcIsoText(const InternalVariables::TimePoint& date) {
    const std::time_t utcSeconds = std::chrono::system_clock::to_time_t(date);
    std::tm utcCalendarTime{};
#if defined(_WIN32)
    if (gmtime_s(&utcCalendarTime, &utcSeconds) != 0) {
        return "<invalid-utc-time>";
    }
#else
    if (gmtime_r(&utcSeconds, &utcCalendarTime) == nullptr) {
        return "<invalid-utc-time>";
    }
#endif

    std::ostringstream dateStream;
    dateStream << std::setfill('0')
               << std::setw(4) << (utcCalendarTime.tm_year + k_tm_year_offset)
               << '-' << std::setw(2) << (utcCalendarTime.tm_mon + 1)
               << '-' << std::setw(2) << utcCalendarTime.tm_mday
               << 'T' << std::setw(2) << utcCalendarTime.tm_hour
               << ':' << std::setw(2) << utcCalendarTime.tm_min
               << ':' << std::setw(2) << utcCalendarTime.tm_sec
               << 'Z';
    return dateStream.str();
}

[[nodiscard]] std::string describeSunEventFailureCause(const double cosHourAngle) {
    if (cosHourAngle > k_cos_hour_angle_max) {
        return "target solar zenith is not reached on this date at the configured coordinates";
    }
    return "sun stays above the target solar zenith for the full day at the configured coordinates";
}

[[nodiscard]] std::string buildSunEventFailureMessage(
    const std::string_view eventName,
    const InternalVariables::TimePoint& date,
    const InternalVariables::GeoCoordinates& coordinates,
    const double zenithDegrees,
    const bool sunriseEvent,
    const double cosHourAngle) {
    std::ostringstream errorText;
    errorText << "sun event calculation failed"
              << ": event=" << eventName
              << ", cause=" << describeSunEventFailureCause(cosHourAngle)
              << ", cosHourAngle=" << cosHourAngle << " (outside ["
              << k_cos_hour_angle_min << ", " << k_cos_hour_angle_max << "])"
              << ", zenithDegrees=" << zenithDegrees
              << ", sunriseEvent=" << (sunriseEvent ? "true" : "false")
              << ", latitude=" << coordinates.latitude
              << ", longitude=" << coordinates.longitude
              << ", utcDate=" << toUtcIsoText(date);
    return errorText.str();
}

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

    values["/sunrise"] = calculateSunEvent(date, coordinates_, k_solar_zenith_official, true, "/sunrise");
    values["/sunset"] = calculateSunEvent(date, coordinates_, k_solar_zenith_official, false, "/sunset");

    auto calculateOrFallback = [&](const std::string_view targetEventName,
                                   const double zenithDegrees,
                                   const bool sunriseEvent,
                                   const std::string_view fallbackEventName,
                                   const TimePoint& fallbackTime) {
        try {
            values[std::string{targetEventName}] = calculateSunEvent(
                date,
                coordinates_,
                zenithDegrees,
                sunriseEvent,
                targetEventName);
        } catch (const std::runtime_error&) {
            // Keep evaluation stable in summer high-daylight periods where twilight
            // boundaries are physically undefined for the configured latitude/date.
            values[std::string{targetEventName}] = fallbackTime;
            (void)fallbackEventName;
        }
    };

    const auto sunriseTime = std::get<TimePoint>(values.at("/sunrise"));
    const auto sunsetTime = std::get<TimePoint>(values.at("/sunset"));

    calculateOrFallback("/civildawn", k_solar_zenith_civil, true, "/sunrise", sunriseTime);
    calculateOrFallback("/nauticaldawn", k_solar_zenith_nautical, true, "/sunrise", sunriseTime);
    calculateOrFallback("/astronomicaldawn", k_solar_zenith_astronomical, true, "/sunrise", sunriseTime);

    calculateOrFallback("/civildusk", k_solar_zenith_civil, false, "/sunset", sunsetTime);
    calculateOrFallback("/nauticaldusk", k_solar_zenith_nautical, false, "/sunset", sunsetTime);
    calculateOrFallback("/astronomicaldusk", k_solar_zenith_astronomical, false, "/sunset", sunsetTime);

    return values;
}

InternalVariables::TimePoint InternalVariables::calculateSunEvent(
    const TimePoint& date,
    const GeoCoordinates& coordinates,
    const double zenithDegrees,
    const bool sunriseEvent,
    const std::string_view eventName) {
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

    if (cosHourAngle > k_cos_hour_angle_max || cosHourAngle < k_cos_hour_angle_min) {
        throw std::runtime_error(buildSunEventFailureMessage(
            eventName,
            date,
            coordinates,
            zenithDegrees,
            sunriseEvent,
            cosHourAngle));
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
