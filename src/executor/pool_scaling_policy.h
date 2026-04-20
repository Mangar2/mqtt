#pragma once

/**
 * @file pool_scaling_policy.h
 * @brief Pure scaling decision helper for WorkerPool growth.
 */

#include <cstddef>

namespace mqtt {

/**
 * @brief Growth threshold for queue depth pressure.
 */
inline constexpr double k_scale_up_queue_factor = 2.0;

/**
 * @brief Growth threshold for worker busy ratio.
 */
inline constexpr double k_scale_up_busy_ratio_threshold = 0.85;

/**
 * @brief Decide whether an executor pool should grow by one worker.
 *
 * @param queue_depth_avg Rolling average pending queue depth.
 * @param worker_count Current worker thread count.
 * @param busy_ratio Fraction of workers currently executing a job.
 * @param max_threads Maximum allowed worker threads.
 * @return `true` when growth conditions are met.
 */
[[nodiscard]] bool should_grow(double queue_depth_avg, std::size_t worker_count,
                               double busy_ratio,
                               std::size_t max_threads) noexcept;

} // namespace mqtt

