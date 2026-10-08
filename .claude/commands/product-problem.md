Skill for analysing YAHA product problems on production system yahapi.
Caveman language. No format chars. No unneeded words. AI only.

## Goal

User reports product symptom (lamp not switching, no feedback, values missing, UI wrong).
Find cause with evidence. Fix only after user agrees.

## Hard rules

- read-only first. every change on yahapi or yaha2 needs user yes. state what changes, how to undo
- symptom of user is only goal. sub finding is not cause until evidence links it to symptom
- suspect is not cause. say "suspect" until proven
- worked before for months = check runtime state and recent changes first, not code
- no commands for hours is normal. evidence of failure = command present in store but no reaction
- absence of log line is weak evidence. prefer store timestamps and history
- never change fixed addresses. httpmqtt port 8183 on 192.168.0.4 is used by field devices (ESP8266). broker 1883
- yaha2 192.168.0.183 is unfinished backup. yahapi services must not depend on yaha2. no passwordless sudo on yaha2
- before change: check what else uses it (ports, topics, files). before claiming done: measure again
- report to user in user language

## Systems

- yahapi 192.168.0.4 production. ssh pi@yahapi. passwordless sudo
- services systemd: broker httpmqtt autom filestore msgstore opensensemap pushover remotesvc rs485 serialdev valuesvc zwave
- PM2 and brkconn are retired and disabled. must stay off
- GUI served by nginx on yahapi /yahagui/. GUI api base 192.168.0.4. /publish -> httpmqtt 8183. /store -> msgstore 8090. /kvstore and / -> filestore 8210
- zwave stick log: /home/pi/mqtt/zwave/OZW_Log.txt. "Value::Set" = command sent to node. ", Error," ", Warning," = stick problems
- service log: journalctl -u <service>. journal rotates fast under flood

## Message store

msgstore subscribes #, $SYS/#, $MONITOR/#. keeps current value plus bounded history per topic.
Reachable via nginx: http://192.168.0.4/store/<topic prefix>

GET with headers:
- levelamount: depth below prefix. 0 = only prefix node. default 1
- history: true adds history newest first
- reason: true adds reason chain (origin). default true
- time: true adds ISO UTC time. default true

Encoding: $ -> %24. space -> %20. root without prefix returns empty. always give prefix.

Examples:
curl -s -H "levelamount: 3" -H "reason: false" "http://192.168.0.4/store/%24SYS"
curl -s -H "levelamount: 0" -H "history: true" "http://192.168.0.4/store/ground/livingroom/zwave/switch/floodlight/set"
curl -s -X POST -H "content-type: application/json" -d '{"topic":"$SYS/broker/clients","levelAmount":1,"history":true,"reason":false}' http://192.168.0.4/store
POST answer is object with payload array. GET answer is array.

Parse with python3 -c json. print topic value time reason messages. never dump raw json to user.

## Topic areas

- ground/... first/... outdoor/...: devices. <x>/set = command. <x> = state report
- $SYS/broker/...: broker uptime, clients/connected, subscriptions/count, messages/sent, messages/received. interval 30s
- $MONITOR/<service>/status: service running. $MONITOR/zwave/nodes/known, node/<n>/include
- $MONITOR/FileStore/changed: filestore write. value json keyPath source (http-post or filesystem-watch). services reload config on this
- system/<service>/info, system/<service>/error: service events. system/zwave/info "configuration reloaded"
- status/...: presence, alerts, motion latest, battery warnings
- status/broker/connect|disconnect|subscribe|ping/<clientId>: legacy broker client events. check time, may be old

## Reason chain

reason list shows origin, oldest first:
- "Request by User", "request by browser": GUI
- "Rule: <name> ...": automation rule
- "received from zwave network node: <n>": zwave state report from device
- "send by ESP8266", "send by yaha ESP8266 module": field device via httpmqtt 8183
- "received from arduino": serial or rs485 device
- "updated": service internal

## Method

1. capture symptom: device, action, expected, seen, time. ask if missing
2. map topics: find command topic <x>/set and state topic <x>. query both with history and reason
3. walk chain hop by hop, evidence each hop:
   GUI -> nginx /publish -> httpmqtt 8183 -> broker -> subscriber service -> hardware -> state report -> msgstore -> GUI
   - command in store with origin reason = reached broker
   - service journal "<service> <- topic" or stick "Value::Set" = reached service or hardware
   - state topic newer than command = device reacted
4. timeline: last good time, first bad time. compare with restarts (systemctl show -p ActiveEnterTimestamp,NRestarts), deploys (binary mtime, md5), config changes, reboot (uptime)
5. storms: same topic many times per minute in history. examples seen: FileStore/changed every 28s from crash looping service, broker status loop from bridge, log flood "unknown token"
6. silence: device topics with old time. check which address device uses (system/broker/address/*) and if that port listens (ss -tlnp)
7. health snapshot: $SYS/broker/clients/connected, $MONITOR/*/status, systemctl is-active all services, uptime load, journal error counts per service
8. state finding as proven or suspect with evidence list. propose fix with undo. wait for yes
9. after fix: measure same evidence again. say result honest

## Known pitfalls

- config reload in services unsubscribes and resubscribes all topics. frequent reloads can drop subscriptions while connection stays up
- crash looping service with filestore sync posts settings each start. filestore publishes change. other services reload
- topic names containing "error" or "failed" (read error code, removefailednode) are not errors. filter by [error] tag
- msgstore journal logs every incoming message with reason. good second source when store history is trimmed
- yaha2 services may still publish into yahapi broker. check reason and source before trusting a change event
