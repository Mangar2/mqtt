#include "yaha/mqtt_component/mqtt_component.h"

namespace yaha {

PublishResult PublishResult::ok() {
    return PublishResult{true, PublishFailureCategory::None, {}};
}

PublishResult PublishResult::fail(const PublishFailureCategory categoryValue,
                                  std::string reasonText) {
    return PublishResult{false, categoryValue, std::move(reasonText)};
}

IMqttComponent::~IMqttComponent() = default;

void IMqttComponent::setPublishCallback(PublishCallback callback) {
    PublishCallback consumedCallback{std::move(callback)};
    (void)consumedCallback;
}

} // namespace yaha
