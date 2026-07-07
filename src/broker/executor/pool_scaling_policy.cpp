#include "executor/pool_scaling_policy.h"

namespace mqtt {

bool should_grow(double queue_depth_avg, std::size_t worker_count,
                 double busy_ratio, std::size_t max_threads) noexcept {
  if (worker_count == 0U) {
    return false;
  }
  if (worker_count >= max_threads) {
    return false;
  }
  if (busy_ratio <= k_scale_up_busy_ratio_threshold) {
    return false;
  }
  const double queue_pressure_threshold =
      static_cast<double>(worker_count) * k_scale_up_queue_factor;
  return queue_depth_avg > queue_pressure_threshold;
}

} // namespace mqtt

