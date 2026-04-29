#include "yaha/mqtt_component/mqtt_component.h"

namespace yaha {

IMqttComponent::~IMqttComponent() = default;

void IMqttComponent::setPublishCallback(PublishCallback callback) {
    (void)callback;
}

} // namespace yaha
