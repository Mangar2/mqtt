# Bug: http-publish-not-received

## user report

Hier ist das beobachtete verhalten. Es werden zwei werte mit gleichem topic mit ausreichend Abstand an den httpinterfaceclient gescickt. Er sendet beide weiter. Er erste wert wird als eingehende meldung geloggt und der zweite wert wird nicht mehr als eigehende Meldung geloggt. Hier sind die loggings: Das wird vom http interface gesendet: May 12 20:46:32 yaha2 yahahttpmqttinterfaceclient[22321]: yahahttpmqttinterfaceclient
May 12 20:46:32 yaha2 yahahttpmqttinterfaceclient[22321]:   listener: 0.0.0.0:8092
May 12 20:46:32 yaha2 yahahttpmqttinterfaceclient[22321]:   mqtt: 127.0.0.1:1883 clientId=yaha-client
May 12 20:46:32 yaha2 yahahttpmqttinterfaceclient[22321]:   compatibility: publish.php=on legacyResponse=off
May 12 20:46:32 yaha2 yahahttpmqttinterfaceclient[22321]:   signal: waiting for SIGINT/SIGTERM
May 12 20:46:53 yaha2 yahahttpmqttinterfaceclient[22321]: http_mqtt_interface_client[in] method=POST endpoint=/publish
May 12 20:46:53 yaha2 yahahttpmqttinterfaceclient[22321]: http_mqtt_interface_client[out] broker_publish topic=ground/wardrobe/floorHeating/lowtemperature/set qos=1 retain=0 dup=0 value=25
May 12 20:47:54 yaha2 yahahttpmqttinterfaceclient[22321]: http_mqtt_interface_client[in] method=POST endpoint=/publish
May 12 20:47:54 yaha2 yahahttpmqttinterfaceclient[22321]: http_mqtt_interface_client[out] broker_publish topic=ground/wardrobe/floorHeating/lowtemperature/set qos=1 retain=0 dup=0 value=23 Hier der log vom ValueStore: May 12 20:46:22 yaha2 yahavalueserviceclient[21850]: yahavalueserviceclient
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   config: /home/pi/mqtt/valueservice/valueservice.ini
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   mqtt: 127.0.0.1:1883 clientId=yahavalueserviceclient
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   filestore: enabled=1 host=127.0.0.1:8210 keyPath=/valueservice/values
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   topics: monitorPrefix=$MONITOR/FileStore subscribeQos=1
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   mqtt: start clientId=yahavalueserviceclient broker=127.0.0.1:1883
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   mqtt: connect clientId=yahavalueserviceclient broker=127.0.0.1:1883
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   mqtt: connected clientId=yahavalueserviceclient broker=127.0.0.1:1883
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   mqtt: subscribe topic=$MONITOR/FileStore/# qos=1
May 12 20:46:22 yaha2 yahavalueserviceclient[21850]:   mqtt: subscribe topic=ground/wardrobe/floorHeating/lowtemperature/set qos=1
May 12 20:46:53 yaha2 yahavalueserviceclient[21850]: value_service[in] topic=ground/wardrobe/floorHeating/lowtemperature/set qos=1 retain=0 value=25
May 12 20:46:53 yaha2 yahavalueserviceclient[21850]: value_service[out] topic=ground/wardrobe/floorHeating/lowtemperature qos=1 retain=1 value=25
May 12 20:46:53 yaha2 yahavalueserviceclient[21850]: value_service[in] topic=$MONITOR/FileStore/changed qos=1 retain=0 value={"keyPath":"/valueservice/values","filename":"4711897108117101115101114118105991014711897108117101115","directory":"./data","changeType":"changed","timestamp":1778611613068,"source":"http-post"}
May 12 20:46:53 yaha2 yahavalueserviceclient[21850]: value_service[out] topic=ground/wardrobe/floorHeating/lowtemperature qos=1 retain=1 value=25 reason="reloaded after valuestore file change"
May 12 20:46:53 yaha2 yahavalueserviceclient[21850]: value_service[in] topic=$MONITOR/FileStore/changed qos=1 retain=0 value={"keyPath":null,"filename":"4711897108117101115101114118105991014711897108117101115","directory":"./data","changeType":"changed","timestamp":1778611613228,"source":"filesystem-watch"}

## test case

Dedizierter Repro-Test im Bug-Ordner:

Run command:
```
python3 spec/bug/http-publish-not-received/repro.py
```

Observed result on local workspace:
```
FAIL: production symptom not reproduced; second inbound set log is present
	topic=ground/wardrobe/floorHeating/lowtemperature/set
	interaction=FileStore reload sequence reproduced
	hit: value_service[in] topic=ground/wardrobe/floorHeating/lowtemperature/set qos=1 retain=0 value=25
	hit: value_service[in] topic=ground/wardrobe/floorHeating/lowtemperature/set qos=1 retain=0 value=23
```

Interpretation for bug process:
- local run reproduces the full FileStore/ValueService interaction chain:
	- value_service[in] on /set
	- value_service[out] persisted value
	- monitor changed event source=http-post with keyPath under /valueservice/values
	- reload publish reason="reloaded after valuestore file change"
	- monitor changed event source=filesystem-watch with keyPath=null
- production symptom (second inbound missing) is still not reproduced locally

## scope

### allow list

- production symptom timeline from provided logs only
- HTTP Interface publish path for topic ground/wardrobe/floorHeating/lowtemperature/set
- ValueService inbound logging for same topic only
- ValueService monitor reload events in same timeline ($MONITOR/FileStore/changed)

### deny list

- no QoS2 analysis
- no retained handling outside observed log lines
- no broker internal module deep-dive (subscription_manager, message_router, store)
- no payload-format mutation analysis in this bug scope unless directly needed for missing second inbound log symptom
- no changes to production configuration assumptions without explicit evidence

## confirmed facts

1. HTTP Interface process started and stayed up during both publish events (same pid 22321).
2. ValueService process started and stayed up during shown window (same pid 21850).
3. ValueService connected to broker 127.0.0.1:1883 and subscribed at startup to:
	- $MONITOR/FileStore/#
	- ground/wardrobe/floorHeating/lowtemperature/set
4. At 20:46:53 HTTP Interface logged inbound POST /publish and outbound broker_publish for topic ground/wardrobe/floorHeating/lowtemperature/set value=25.
5. At 20:46:53 ValueService logged matching inbound message on same /set topic with value=25.
6. At 20:46:53 ValueService logged two monitor changed inbound events and one reload publish event.
7. At 20:47:54 HTTP Interface logged second inbound POST /publish and outbound broker_publish for same /set topic with value=23.
8. In the provided ValueService log excerpt there is no visible value_service[in] line for the second /set publish at 20:47:54.
9. User confirmed the provided service logs are the complete log output since service restart.
10. Integration repro confirms the update interaction between FileStore and ValueService exactly as observed in production logs.
11. This interaction is marked as a strong hint for root cause analysis: ValueService write -> FileStore changed event -> ValueService reload -> FileStore watch event with keyPath=null.
12. Code cause found: FileStore watcher events were emitted with keyPath=null by design because watcherLoop passed nullptr for all filesystem-watch events.
13. sessions.dat, sessions.bak, retained.dat, retained.bak are internal files without logical key path mapping; keyPath=null is expected for these files.
14. For the encoded values file itself, keyPath can be known and is now propagated in filesystem-watch events.

## hypothesis

no hard suspect

Step 4 short analysis result:
- Based on complete logs alone, the missing second value_service[in] cannot yet be causally linked to a single code path with proof.
- Current evidence shows publish emission at HTTP interface but missing inbound log at ValueService for second event.
- Trace evidence is required for proof-level causality.

Strong hint (not yet proof):
- The FileStore/ValueService update interaction is abnormal and highly suspicious for the bug mechanism.
- Priority suspect chain for next trace step: handleSetMessage persistence and monitor-triggered reload path around keyPath=null events.

## resolution

Partial fix implemented for keyPath handling:

- FileStore now tracks known filename->keyPath mappings for files written through HTTP POST.
- filesystem-watch events now include keyPath for known mapped files (instead of always null).
- ValueService now ignores monitoring events with source="filesystem-watch" to prevent reload loops when filesystem-watch events include keyPath.

Files touched:
- src/yaha/file_store/file_store.h
- src/yaha/file_store/file_store.cpp
- src/yaha/file_store/test/file_store_test.cpp
- src/yaha/value_service/value_service_component.cpp
- src/yaha/value_service/test/value_service_component_test.cpp
