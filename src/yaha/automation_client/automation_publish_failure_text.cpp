#include "yaha/automation_client/automation_publish_failure_text.h"

namespace yaha::automation_publish_failure_text {

std::string toText(const PublishFailureCategory categoryValue) {
    switch (categoryValue) {
    case PublishFailureCategory::None:
        return "none";
    case PublishFailureCategory::Disconnected:
        return "disconnected";
    case PublishFailureCategory::AckTimeout:
        return "ack_timeout";
    case PublishFailureCategory::WriteFailed:
        return "write_failed";
    case PublishFailureCategory::CallbackMissing:
        return "callback_missing";
    case PublishFailureCategory::Unknown:
        return "unknown";
    }

    return "unknown";
}

} // namespace yaha::automation_publish_failure_text
