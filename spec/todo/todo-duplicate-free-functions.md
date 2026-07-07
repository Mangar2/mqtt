# TODO: Consolidate Duplicate Free-Function Names (src/, non-test)

## Scope

AI-generated C++ code repeatedly reinvents small free-function helpers
instead of reusing an existing one (e.g. a `toLower`-style helper defined
independently in several files). This list was produced by
`test/check_duplicate_functions.py` (default scan: `src/`, test
directories excluded) and enumerates every free-function name that is
defined in more than one file.

For each entry: check whether the implementations are truly identical
(or should be), decide on one canonical home (existing shared helper file
in that module, or a new one), move the logic there, and update all call
sites to use it. Some entries may be legitimate (e.g. deliberately
independent per-module logic with an incidental name clash) - verify
before merging.

Regenerate this list with:
```
python3 test/check_duplicate_functions.py
```

## Duplicates (76)

- [ ] `appendReasonsPreservingOrder` (2 occurrences)
  - src/yaha/broker_connector/source_http_adapter.cpp:167
  - src/yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_helpers.cpp:218

- [ ] `append_frame` (2 occurrences)
  - src/broker/connection/handshake_step.cpp:32
  - src/broker/connection/runtime_step.cpp:21

- [ ] `binary_from_text_with_encoding_or_throw` (2 occurrences)
  - src/test_client/test_client_scenario_runner.cpp:301
  - src/test_client_main.cpp:285

- [ ] `close_backend_fd` (2 occurrences)
  - src/network/io_reactor_epoll.cpp:17
  - src/network/io_reactor_kqueue.cpp:19

- [ ] `close_fd` (2 occurrences)
  - src/network/io_reactor_epoll.cpp:23
  - src/network/io_reactor_kqueue.cpp:25

- [ ] `close_socket_handle` (4 occurrences)
  - src/client/connection_negotiator.cpp:37
  - src/client/connection_negotiator.cpp:45
  - src/network/socket_ops.cpp:27
  - src/network/socket_ops.cpp:39

- [ ] `configureFileStoreClientTimeouts` (3 occurrences)
  - src/yaha/zwave_client/zwave_client_app.cpp:437
  - src/yaha_automationclient_main.cpp:27
  - src/yaha_valueserviceclient_main.cpp:28

- [ ] `create_wake_pipe` (2 occurrences)
  - src/network/io_reactor_epoll.cpp:45
  - src/network/io_reactor_kqueue.cpp:47

- [ ] `decodeGenreOrDefault` (2 occurrences)
  - src/yaha/zwave_client/openzwave_notification_bridge.cpp:29
  - src/yaha/zwave_client/openzwave_write_dispatcher.cpp:41

- [ ] `decode_base64_to_bytes_or_throw` (2 occurrences)
  - src/test_client/test_client_scenario_runner.cpp:268
  - src/test_client_main.cpp:250

- [ ] `decode_hex_to_bytes_or_throw` (2 occurrences)
  - src/test_client/test_client_scenario_runner.cpp:245
  - src/test_client_main.cpp:226

- [ ] `decode_properties` (2 occurrences)
  - src/broker/persistence/retained_message_persistence.cpp:89
  - src/codec/properties/properties_codec.cpp:198

- [ ] `decode_property_value` (2 occurrences)
  - src/broker/persistence/retained_message_persistence.cpp:50
  - src/codec/properties/properties_codec.cpp:120

- [ ] `drain_wake_pipe` (2 occurrences)
  - src/network/io_reactor_epoll.cpp:61
  - src/network/io_reactor_kqueue.cpp:63

- [ ] `encode_properties` (2 occurrences)
  - src/broker/persistence/retained_message_persistence.cpp:80
  - src/codec/properties/properties_codec.cpp:157

- [ ] `encode_property_value` (2 occurrences)
  - src/broker/persistence/retained_message_persistence.cpp:20
  - src/codec/properties/properties_codec.cpp:95

- [x] `endpointToString` (3 occurrences)
  - src/yaha/serial_device/serial_device_message.cpp:4
  - src/yaha/serial_device/serial_device_serial_to_mqtt_mapper.cpp:13
  - src/yaha/serial_device/serial_device_wire_serializer.cpp:11

- [ ] `endsWithSetSuffix` (2 occurrences)
  - src/yaha/automation_client/automation_control_topics.cpp:17
  - src/yaha/value_service/value_service_component.cpp:50

- [ ] `escapeJsonString` (2 occurrences)
  - src/yaha/broker_connector/relay_component.cpp:15
  - src/yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_helpers.cpp:72

- [ ] `executeCommand` (2 occurrences)
  - src/yaha/opensensemap_client/opensensemap_client_app.cpp:218
  - src/yaha/pushover_client/pushover_client_app.cpp:52

- [ ] `find_receive_maximum` (2 occurrences)
  - src/broker/connection/connection_flow_support.cpp:120
  - src/client/connection_negotiator.cpp:49

- [ ] `find_server_keep_alive` (2 occurrences)
  - src/broker/connection/handshake_step.cpp:18
  - src/client/connection_negotiator.cpp:79

- [ ] `genreNameForLog` (2 occurrences)
  - src/yaha/zwave_client/openzwave_notification_bridge.cpp:44
  - src/yaha/zwave_client/openzwave_runtime_driver_port.cpp:94

- [ ] `handleSignal` (5 occurrences)
  - src/yaha/mqtt_client/mqtt_client_runtime.cpp:11
  - src/yaha_automationclient_main.cpp:22
  - src/yaha_remoteserviceclient_main.cpp:26
  - src/yaha_valueserviceclient_main.cpp:23
  - src/yaha_zwaveclient_main.cpp:25

- [ ] `hex_nibble_or_throw` (2 occurrences)
  - src/test_client/test_client_scenario_runner.cpp:232
  - src/test_client_main.cpp:213

- [ ] `isFileStoreDataAvailable` (2 occurrences)
  - src/yaha_automationclient_main.cpp:53
  - src/yaha_valueserviceclient_main.cpp:54

- [ ] `localDayStart` (2 occurrences)
  - src/yaha/automation/expression_evaluator_helpers.cpp:331
  - src/yaha/automation_client/rule_runtime_engine.cpp:56

- [ ] `logConfigFallbackWarning` (13 occurrences)
  - src/yaha/automation_client/automation_client_app.cpp:53
  - src/yaha/broker_connector_client/broker_connector_client_app.cpp:24
  - src/yaha/file_store_client/file_store_client_app.cpp:13
  - src/yaha/message/message_log_service.cpp:10
  - src/yaha/message_store_client/message_store_client_app.cpp:24
  - src/yaha/mqtt_client/mqtt_client_config.cpp:14
  - src/yaha/opensensemap_client/opensensemap_client_app.cpp:24
  - src/yaha/pushover_client/pushover_client_app.cpp:23
  - src/yaha/remote_service_client/remote_service_client_app.cpp:15
  - src/yaha/rs485_interface_client/rs485_interface_client_app.cpp:380
  - src/yaha/serial_device_client/serial_device_client_config.cpp:35
  - src/yaha/value_service_client/value_service_client_app.cpp:14
  - src/yaha/zwave_client/zwave_client_app.cpp:68

- [ ] `logError` (2 occurrences)
  - src/yaha/opensensemap/opensensemap_component.cpp:34
  - src/yaha/pushover/pushover_component.cpp:23

- [ ] `logHttpError` (2 occurrences)
  - src/yaha/opensensemap/opensensemap_component.cpp:41
  - src/yaha/pushover/pushover_component.cpp:30

- [ ] `main` (13 occurrences)
  - src/main.cpp:386
  - src/test_client_main.cpp:1586
  - src/yaha_automationclient_main.cpp:155
  - src/yaha_brokerconnectorclient_main.cpp:73
  - src/yaha_filestoreclient_main.cpp:86
  - src/yaha_httpmqttinterfaceclient_main.cpp:51
  - src/yaha_opensensemapclient_main.cpp:58
  - src/yaha_pushoverclient_main.cpp:58
  - src/yaha_remoteserviceclient_main.cpp:118
  - src/yaha_rs485interfaceclient_main.cpp:75
  - src/yaha_serialdeviceclient_main.cpp:77
  - src/yaha_valueserviceclient_main.cpp:151
  - src/yaha_zwaveclient_main.cpp:141

- [ ] `makeStandardJsonHeaders` (2 occurrences)
  - src/yaha/broker_connector/source_http_adapter.cpp:311
  - src/yaha/http_mqtt_interface/http_mqtt_interface_contracts.cpp:53

- [ ] `make_error_msg` (2 occurrences)
  - src/network/tcp_listener_posix.cpp:22
  - src/network/tcp_listener_win32.cpp:32

- [ ] `matchesTopicFilter` (2 occurrences)
  - src/yaha/automation_client/rule_runtime_engine.cpp:209
  - src/yaha/message/message_log_filter.cpp:27

- [ ] `parseCli` (2 occurrences)
  - src/yaha_rs485interfaceclient_main.cpp:24
  - src/yaha_serialdeviceclient_main.cpp:24

- [ ] `parseCommandMapSection` (2 occurrences)
  - src/yaha/rs485_interface_client/rs485_interface_client_app.cpp:313
  - src/yaha/serial_device_client/serial_device_client_config.cpp:101

- [ ] `parseCurlOutput` (2 occurrences)
  - src/yaha/opensensemap_client/opensensemap_client_app.cpp:234
  - src/yaha/pushover_client/pushover_client_app.cpp:68

- [ ] `parseIsoTimezoneOffset` (2 occurrences)
  - src/yaha/automation/expression_evaluator_helpers.cpp:137
  - src/yaha/message_store/iso_timestamp_parser.cpp:193

- [ ] `parseQosField` (2 occurrences)
  - src/yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_helpers.cpp:227
  - src/yaha/remote_service/remote_service_component.cpp:41

- [ ] `parseTraceLevel` (2 occurrences)
  - src/yaha/rs485_interface_client/rs485_interface_client_app.cpp:85
  - src/yaha/serial_device_client/serial_device_client_config.cpp:66

- [ ] `parse_bool_literal_or_throw` (2 occurrences)
  - src/test_client/test_client_cli.cpp:58
  - src/test_client_main.cpp:504

- [ ] `printStartupConfiguration` (5 occurrences)
  - src/yaha_automationclient_main.cpp:130
  - src/yaha_brokerconnectorclient_main.cpp:51
  - src/yaha_filestoreclient_main.cpp:65
  - src/yaha_valueserviceclient_main.cpp:131
  - src/yaha_zwaveclient_main.cpp:113

- [ ] `printStartupSummary` (3 occurrences)
  - src/yaha_remoteserviceclient_main.cpp:76
  - src/yaha_rs485interfaceclient_main.cpp:54
  - src/yaha_serialdeviceclient_main.cpp:54

- [ ] `printUsage` (12 occurrences)
  - src/yaha_automationclient_main.cpp:92
  - src/yaha_brokerconnectorclient_main.cpp:19
  - src/yaha_filestoreclient_main.cpp:25
  - src/yaha_httpmqttinterfaceclient_main.cpp:17
  - src/yaha_msgstoreclient_main.cpp:44
  - src/yaha_opensensemapclient_main.cpp:18
  - src/yaha_pushoverclient_main.cpp:18
  - src/yaha_remoteserviceclient_main.cpp:38
  - src/yaha_rs485interfaceclient_main.cpp:16
  - src/yaha_serialdeviceclient_main.cpp:16
  - src/yaha_valueserviceclient_main.cpp:93
  - src/yaha_zwaveclient_main.cpp:75

- [ ] `publishFailureCategoryToText` (2 occurrences)
  - src/yaha/file_store/file_store.cpp:47
  - src/yaha/value_service/value_service_component.cpp:27

- [ ] `publishStatus` (3 occurrences)
  - src/yaha_automationclient_main.cpp:45
  - src/yaha_valueserviceclient_main.cpp:46
  - src/yaha_zwaveclient_main.cpp:38

- [ ] `qosToLogText` (2 occurrences)
  - src/yaha/automation_client/automation_message_values.cpp:22
  - src/yaha/file_store/file_store.cpp:76

- [ ] `qos_from_u8_or_throw` (2 occurrences)
  - src/test_client/test_client_scenario_runner.cpp:400
  - src/test_client_main.cpp:66

- [ ] `requireNonEmptyString` (2 occurrences)
  - src/yaha/rs485_interface_client/rs485_interface_client_app.cpp:49
  - src/yaha/serial_device_client/serial_device_client_config.cpp:50

- [ ] `requireSetting` (2 occurrences)
  - src/yaha/remote_service_client/remote_service_client_app.cpp:31
  - src/yaha/zwave_client/zwave_client_app.cpp:213

- [ ] `requireUint8` (3 occurrences)
  - src/yaha/zwave_client/openzwave_notification_bridge.cpp:16
  - src/yaha/zwave_client/openzwave_runtime_driver_port.cpp:87
  - src/yaha/zwave_client/openzwave_write_dispatcher.cpp:20

- [ ] `send_disconnect_best_effort` (2 occurrences)
  - src/test_client/test_client_scenario_runner.cpp:455
  - src/test_client_main.cpp:1029

- [ ] `set_nonblocking_cloexec` (2 occurrences)
  - src/network/io_reactor_epoll.cpp:29
  - src/network/io_reactor_kqueue.cpp:31

- [ ] `shellQuote` (2 occurrences)
  - src/yaha/opensensemap_client/opensensemap_client_app.cpp:205
  - src/yaha/pushover_client/pushover_client_app.cpp:39

- [ ] `split` (2 occurrences)
  - src/yaha/rs485_interface_client/rs485_interface_client_app.cpp:39
  - src/yaha/serial_device_client/serial_device_client_config.cpp:25

- [ ] `splitTopic` (2 occurrences)
  - src/yaha/mqtt_client/mqtt_client.cpp:28
  - src/yaha/zwave_controller/zwave_controller_topic_utils.cpp:51

- [x] `startsWith` (3 occurrences)
  - src/yaha/file_store/file_store.cpp:33
  - src/yaha/message_store/message_store.cpp:73
  - src/yaha/zwave/zwave_service_component.cpp:31

- [x] `startsWithText` (4 occurrences)
  - src/yaha/automation_client/automation_control_topics.cpp:13
  - src/yaha/opensensemap/opensensemap_component.cpp:20
  - src/yaha/remote_service/remote_service_component.cpp:132
  - src/yaha/value_service/value_service_component.cpp:46

- [ ] `toIsoTimestamp` (2 occurrences)
  - src/yaha/automation/single_rule_processor.cpp:83
  - src/yaha/message_store/message_store.cpp:289

- [ ] `toLocalCalendarTime` (2 occurrences)
  - src/yaha/automation/expression_evaluator_helpers.cpp:259
  - src/yaha/automation_client/rule_runtime_engine.cpp:40

- [ ] `toLower` (6 occurrences)
  - src/yaha/automation/expression_evaluator_helpers.cpp:294
  - src/yaha/broker_connector/source_http_adapter.cpp:46
  - src/yaha/message_store/message_store.cpp:66
  - src/yaha/message_store/message_store_json_parser.cpp:36
  - src/yaha/zwave/zwave_service_component.cpp:71
  - src/yaha/zwave_client/openzwave_write_dispatcher.cpp:56

- [ ] `toLowerCopy` (4 occurrences)
  - src/yaha/http_mqtt_interface/http_mqtt_interface_contracts.cpp:23
  - src/yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_helpers.cpp:32
  - src/yaha/rs485_interface/rs485_topic_mapper.cpp:19
  - src/yaha/serial_device/serial_device_mqtt_to_serial_mapper.cpp:16

- [ ] `to_fd` (4 occurrences)
  - src/client/connection_negotiator.cpp:41
  - src/network/socket_ops.cpp:31
  - src/network/tcp_connection_posix.cpp:17
  - src/network/tcp_listener_posix.cpp:49

- [ ] `to_socket` (5 occurrences)
  - src/client/connection_negotiator.cpp:32
  - src/network/io_reactor_win32.cpp:16
  - src/network/socket_ops.cpp:17
  - src/network/tcp_connection_win32.cpp:10
  - src/network/tcp_listener_win32.cpp:43

- [ ] `to_string` (2 occurrences)
  - src/broker/monitoring/trace_level.cpp:12
  - src/test_client/test_client_profile.cpp:200

- [ ] `trim` (4 occurrences)
  - src/broker/transport/websocket_handshake.cpp:157
  - src/yaha/broker_connector/source_http_adapter.cpp:32
  - src/yaha/message_store/message_store.cpp:52
  - src/yaha/message_store/message_store_json_parser.cpp:15

- [ ] `trimCopy` (7 occurrences)
  - src/yaha/http_mqtt_interface/http_mqtt_interface_contracts.cpp:35
  - src/yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_helpers.cpp:18
  - src/yaha/ini/ini_document.cpp:17
  - src/yaha/pushover/pushover_component.cpp:38
  - src/yaha/rs485_interface_client/rs485_interface_client_app.cpp:21
  - src/yaha/serial_device_client/serial_device_client_config.cpp:16
  - src/yaha/zwave_client/zwave_client_app.cpp:24

- [ ] `trim_copy` (2 occurrences)
  - src/broker/monitoring/trace_runtime_command.cpp:20
  - src/test_client/test_client_profile.cpp:11

- [ ] `tryLoadSubscriptionsFromIni` (2 occurrences)
  - src/yaha/mqtt_client/mqtt_client_config.cpp:127
  - src/yaha/pushover_client/pushover_client_app.cpp:214

- [ ] `tryParseCli` (9 occurrences)
  - src/yaha_automationclient_main.cpp:100
  - src/yaha_brokerconnectorclient_main.cpp:26
  - src/yaha_filestoreclient_main.cpp:33
  - src/yaha_httpmqttinterfaceclient_main.cpp:24
  - src/yaha_opensensemapclient_main.cpp:26
  - src/yaha_pushoverclient_main.cpp:26
  - src/yaha_remoteserviceclient_main.cpp:46
  - src/yaha_valueserviceclient_main.cpp:101
  - src/yaha_zwaveclient_main.cpp:83

- [ ] `valueToJsonValue` (2 occurrences)
  - src/yaha/message_store/message_store.cpp:322
  - src/yaha/serial_device/serial_device_wire_serializer.cpp:52

- [ ] `valueToLogText` (2 occurrences)
  - src/yaha/automation_client/automation_message_values.cpp:12
  - src/yaha/file_store/file_store.cpp:66

- [ ] `valueToString` (3 occurrences)
  - src/yaha/automation/expression_evaluator_helpers.cpp:388
  - src/yaha/serial_device/serial_device_message.cpp:14
  - src/yaha/serial_device/serial_device_wire_serializer.cpp:21

- [ ] `valueToText` (2 occurrences)
  - src/yaha/opensensemap/opensensemap_component.cpp:24
  - src/yaha/rs485_interface/rs485_topic_mapper.cpp:70

- [ ] `waitForBrokerConnection` (3 occurrences)
  - src/yaha_automationclient_main.cpp:37
  - src/yaha_valueserviceclient_main.cpp:38
  - src/yaha_zwaveclient_main.cpp:30

- [ ] `waitForFileStoreStartupData` (2 occurrences)
  - src/yaha_automationclient_main.cpp:64
  - src/yaha_valueserviceclient_main.cpp:65

