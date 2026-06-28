#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"

#include "yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_internal.h"

namespace yaha {

HttpMqttInterfaceHandlerRegistry makeHttpMqttInterfaceHandlerRegistryV1() {
	HttpMqttInterfaceHandlerRegistry registry{};

	registry.connectRequests[std::string{http_mqtt_ops_internal::k_versionValue}] = buildConnectV1Request;
	registry.connectResponses[std::string{http_mqtt_ops_internal::k_versionValue}] = buildConnectV1Response;

	registry.disconnectRequests[std::string{http_mqtt_ops_internal::k_versionValue}] = buildDisconnectV1Request;
	registry.disconnectResponses[std::string{http_mqtt_ops_internal::k_versionValue}] = buildDisconnectV1Response;

	registry.publishRequests[std::string{http_mqtt_ops_internal::k_versionValue}] = buildPublishV1Request;
	registry.publishResponses[std::string{http_mqtt_ops_internal::k_versionValue}] = buildPublishV1Response;

	registry.pubrelRequests[std::string{http_mqtt_ops_internal::k_versionValue}] = buildPubrelV1Request;
	registry.pubrelResponses[std::string{http_mqtt_ops_internal::k_versionValue}] = buildPubrelV1Response;

	registry.subscribeRequests[std::string{http_mqtt_ops_internal::k_versionValue}] = buildSubscribeV1Request;
	registry.subscribeResponses[std::string{http_mqtt_ops_internal::k_versionValue}] = buildSubscribeV1Response;

	registry.unsubscribeRequests[std::string{http_mqtt_ops_internal::k_versionValue}] = buildUnsubscribeV1Request;
	registry.unsubscribeResponses[std::string{http_mqtt_ops_internal::k_versionValue}] = buildUnsubscribeV1Response;

	return registry;
}

HttpMqttInterfaces makeHttpMqttInterfacesV1() {
	return HttpMqttInterfaces{makeHttpMqttInterfaceHandlerRegistryV1()};
}

} // namespace yaha
