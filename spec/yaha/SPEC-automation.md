# Automation

## Purpose

Automation is the YAHA rule engine component. It evaluates event-driven and time-driven rules and emits MQTT messages when rule conditions become true.

The target of this specification is behavioral parity with the legacy Automation module, so the C++ reimplementation can be built from this document without consulting legacy code.

## Role in the system

Automation is a domain component behind the YAHA MQTT runtime boundary defined by SPEC-IMqttComponent.

It consumes:
- incoming MQTT messages,
- current date/time context,
- geo context (longitude/latitude),
- configured rule definitions.

It produces:
- domain output messages from rules,
- runtime rule-management acknowledgment messages.

Automation itself does not execute device actions. It only publishes messages; device or service actors perform the action.

## Standalone program structure

Automation standalone executable contains two composed parts:
1. Generic YAHA MQTT client runtime.
2. Automation domain component.

Main wiring model:
1. Create Automation with sanitized config.
2. Resolve and load initial rules.
3. Ask Automation for subscriptions.
4. Start MQTT runtime loop.
5. Route each incoming MQTT message to Automation handleMessage.
6. Publish all returned Message objects.
7. Register periodic execution of processTasks using intervalInSeconds.

MQTT runtime capability requirement:
- The MQTT client runtime must provide a periodic sender/timer mechanism that calls processTasks at intervalInSeconds cadence and publishes returned messages.
- If this capability is not present in the current MQTT client implementation, it must be added.

The component must not implement broker reconnect/session policy. The runtime must not know Automation internals.

## MQTT topic namespace mapping

Legacy implementation uses control topics under $SYS.

For YAHA reimplementation, all legacy $SYS control topics are mapped 1:1 to $MONITOR topics:

- $SYS/simulation/# -> $MONITOR/simulation/#
- $SYS/simulation/date -> $MONITOR/simulation/date
- $SYS/simulation/end -> $MONITOR/simulation/end
- $SYS/automation/# -> $MONITOR/automation/#
- $SYS/automation/rules/<name>/set -> $MONITOR/automation/rules/<name>/set
- $SYS/automation/rules/<name> -> $MONITOR/automation/rules/<name>
- default presence topic $SYS/presence -> $MONITOR/presence
- default presence input topic $SYS/presence/set -> $MONITOR/presence/set

No YAHA component may subscribe/publish $SYS topics.

## Subscriptions

getSubscriptions returns map topicPattern -> qos.

Subscription sources:
1. Static motion topics from config.motionTopics, each with config.subscribeQoS.
2. Dynamic variable topics discovered from rule analysis (see Rule variable discovery), each with config.subscribeQoS.
3. Monitoring control channels:
   - $MONITOR/simulation/# with qos 1.
   - $MONITOR/automation/# with qos 1.

Dynamic variable topic inclusion rule:
- Include variable topic only if it is not already matched by any motion topic pattern.

## Published messages

Automation emits two categories of messages.

1. Rule output messages
- Produced by rule evaluation.
- Topic/value/qos derive from rule definition and rules engine behavior.

2. Rule-management acknowledgments
- Topic: $MONITOR/automation/rules/<ruleName>
- qos: always 1
- payloads:
  - JSON string of accepted/updated rule,
  - invalid rule,
  - deleted

## External interfaces

Public component interface:
- prepare(config, automation?) -> Automation
- setRules(rulesTree)
- getSubscriptions() -> map topicPattern -> qos
- processTasks(date = now, simulation = false) -> { messages, usedVariables }
- handleMessage(message) -> Message[]
- filestoreOptions getter

No HTTP server API is exposed by Automation itself.

Automation uses optional outbound HTTP client integration for file-store read/write of rule trees.
File-store contract reference: [SPEC-filestore.md](./SPEC-filestore.md).

## Configuration

## Schema contract

Required:
- motionTopics: string[]
- longitude: number
- latitude: number

Optional:
- subscribeQoS: one of 0, 1, 2
- presenceTopic: string
- rules: string or string[]
- intervalInSeconds: integer
- filestore: object
  - use: boolean
  - path: string
  - host: string
  - port: string or integer

Automation client standalone optional logging switches (`[automation]`):
- logIncomingMessages: boolean
- logOutgoingMessages: boolean

Unknown config properties are not allowed by schema.

## Defaults

- motionTopics:
  - +/+/+/motion sensor/detection state
  - $MONITOR/presence/set
- presenceTopic: $MONITOR/presence
- rules: [rules.json]
- intervalInSeconds: 60
- subscribeQoS: 1
- filestore:
  - use: false
  - path: /automation/rules
  - host: localhost
  - port: 8210
- automation client logging:
  - logIncomingMessages: false
  - logOutgoingMessages: false

## Config semantics

- longitude/latitude are required to compute sun/time variables.
- intervalInSeconds defines the periodic execution cadence in seconds for calling processTasks in normal mode.
- Rule checks run in two paths: periodic processTasks cadence and after every incoming message via handleMessage.
- rules can be one file or multiple files.
- filestore.use=true activates file-store-first rule loading and write-back on rule updates.
- Timezone is not configurable in Automation config.
- logIncomingMessages enables logging for all incoming messages handled by Automation client runtime.
- logOutgoingMessages enables logging for all outgoing rule and acknowledgment messages emitted by Automation client runtime.

## Temporal basis and timezone behavior

Code-derived runtime contract:
- Automation evaluates time using runtime Date values passed into processTasks/simulation handling.
- Weekday and time-of-day checks are derived from Date-based operations.
- Automation does not define a separate UTC normalization step.
- Automation does not define a component-level timezone override.

Implementation requirement for parity:
- Use one consistent Date/time representation for all rule-time comparisons and derived variables inside one evaluation path.

## Rule source loading and persistence

## Startup loading order

prepare behavior:
1. Create Automation if not provided.
2. Try readRulesFromFileStore(filestoreOptions) when filestore.use=true.
3. Call setRules(loadedRuleTree).

Compatibility note:
- The previous local file fallback after file-store read failure is retired.
- Rule loading is now file-store-only when `filestore.use=true`.

## Local file loading

Status:
- Local file fallback loading is retired compatibility behavior and must not be used.
- `config.rules` remains schema-compatible for legacy configuration acceptance, but it is not used as runtime fallback source.

## Runtime persistence

On each successful rule-management update/delete message:
- Persist full rule tree to file store via HTTP POST.
- Endpoint path: filestore.path
- Payload: full rule tree as JSON

## Rule tree format

### Tree structure

The rule tree is an arbitrary JSON object hierarchy where rules are organized by domain, location, and function.

```
{
  "domainOrCategory": {
    "location": {
      "function": {
        "rules": {
          "ruleName1": { rule object },
          "ruleName2": { rule object }
        }
      },
      "anotherFunction": {
        "rules": {
          "ruleName3": { rule object }
        }
      }
    },
    "anotherLocation": {
      "rules": {
        "directRuleName": { rule object }
      }
    }
  }
}
```

### Extraction rule

- Parser scans recursively for any property named `rules`.
- All rule objects directly under a `rules` property are extracted.
- `rules` property must appear as a leaf node (no further nesting below it).
- The entire hierarchy above a `rules` property is used to form the rule's full name.

### Rule identity and naming

Rule name is constructed as the dot-separated path of all keys leading to and including the rule key under a `rules` block.

Examples from a rules tree:
```
motion/setReceived                           (under motion -> rules)
ground/kitchen/dishwasher/onMorning          (under ground -> kitchen -> dishwasher -> rules -> onMorning)
ground/wardrobe/ventilation/OnEvening        (under ground -> wardrobe -> ventilation -> rules -> OnEvening)
cellar/boilerroom/washingmachine/SwitchOn    (under cellar -> boilerroom -> washingmachine -> rules)
alert/motion                                 (under alert -> rules)
```

### Runtime semantics

- Rule name uniquely identifies the rule within the rule set.
- Rule update topic path also defines the ruleName: `$MONITOR/automation/rules/<ruleName>/set`
- If update topic has nested segments like `ground/kitchen/dishwasher/onMorning/set`, the ruleName is `ground/kitchen/dishwasher/onMorning`.
- The hierarchical path above the `rules` property is organizational only; it has no semantic effect on rule evaluation.

### Arbitrary hierarchy

- Domains, locations, functions, and naming conventions are user-defined.
- The parser does not enforce naming patterns or hierarchy depth.
- Examples: `motion`, `ground`, `cellar`, `first`, `outdoor`, `alert` are example domain/category names.
- Examples: `kitchen`, `wardrobe`, `livingroom` are example location names.
- Examples: `ventilation`, `dishwasher`, `roller`, `motion` are example function names.

### Non-rules properties

Any JSON object property not named `rules` is ignored by the parser and can be used for documentation or organizational metadata.

### Concrete tree structure example

```json
{
  "motion": {
    "rules": {
      "setReceived": {
        "anyOf": ["$SYS/presence/set", "system/presence/set"],
        "check": "map_1 = (1: awake, on: awake, awake: awake, sleeping: sleeping, default: absent)\n$SYS/presence != map_1($SYS/presence/set)",
        "topic": {...},
        "name": "motion/setReceived"
      },
      "initialize": {
        "check": "$SYS/presence = initial",
        "anyOf": ["+/+/+/motion sensor/detection state"],
        "topic": {...},
        "name": "motion/initialize"
      }
    }
  },
  "ground": {
    "kitchen": {
      "dishwasher": {
        "rules": {
          "onMorning": {
            "time": "6:00",
            "topic": {"ground/kitchen/zwave/switch/dishwasher/set": "on"},
            "name": "ground/kitchen/dishwasher/onMorning"
          }
        }
      }
    },
    "wardrobe": {
      "floorHeating": {
        "rules": {
          "onMorning": {..., "name": "ground/wardrobe/floorHeating/onMorning"},
          "offEvening": {..., "name": "ground/wardrobe/floorHeating/offEvening"}
        }
      },
      "ventilation": {
        "rules": {
          "OnEvening": {..., "name": "ground/wardrobe/ventilation/OnEvening"},
          "OnMorning": {..., "name": "ground/wardrobe/ventilation/OnMorning"}
        }
      }
    }
  },
  "cellar": {
    "boilerroom": {
      "washingmachine": {
        "rules": {
          "SwitchOn": {..., "name": "cellar/boilerroom/washingmachine/SwitchOn"}
        }
      }
    }
  },
  "alert": {
    "rules": {
      "motion": {..., "name": "alert/motion"},
      "nomotion": {..., "name": "alert/nomotion"}
    }
  }
}
```

In this example:
- The rule `motion/setReceived` is found by navigating: root → motion → rules → setReceived
- The rule `ground/kitchen/dishwasher/onMorning` is found by: root → ground → kitchen → dishwasher → rules → onMorning
- The rule `alert/motion` is found by: root → alert → rules → motion

Parser extracts all rule objects from any `rules` property, regardless of hierarchy depth or naming patterns.

## Rule contract

Each rule is a JSON object.
Mandatory field: topic.

Expression-bearing fields (`check`, `value`, `time`) use the Python-style textual DSL defined below.
The previous JSON-array expression tree is replaced by this textual DSL in new rule authoring.

Supported fields:
- topic
- value
- time
- duration
- check
- cooldownInSeconds
- delayInSeconds
- qos
- anyOf
- allOf
- noneOf
- allow
- durationWithoutMovementInMinutes

Not supported field name:
- oneOf (runtime schema and evaluator use anyOf, not oneOf)

## topic field semantics

Accepted shapes:
1. string topic with separate value field.
2. array of topics with shared value field.
3. object map topic -> value (value field optional).

Behavior:
- One rule may emit one or many messages depending on topic shape.

## value field semantics

- Device command/state payload to emit.
- Can be literal or Python-style expression result.
- Optional when topic is object map containing explicit per-topic values.

## time and duration semantics

time:
- Start time constraint.
- Supports HH:MM or HH:MM:SS format.

duration:
- Active window length after time.
- Supports H:MM or HH:MM.
- Default duration when omitted but time is used: 6 hours.

## check semantics

- Additional predicate expression (Python-style DSL).
- Rule emits only when check resolves true.

## Event trigger fields

anyOf:
- At least one listed event topic must be present in recent events.

allOf:
- All listed event topics must be present in recent related events.

noneOf:
- Rule must not trigger if any listed disallowed event is present.

allow:
- Optional allow-list filter for event-driven rules.

durationWithoutMovementInMinutes:
- Additional inactivity constraint.
- Rule can trigger only if time since latest motion >= configured minutes.

## Message delivery control fields

qos:
- Per-message MQTT QoS override for emitted rule messages.
- Default from rule engine behavior is qos 1 when not specified.

cooldownInSeconds:
- Enables periodic resend of identical output while rule remains active.
- Without cooldown, identical topic/value output is deduplicated and only resent on state change.

delayInSeconds:
- Output is emitted only after identical candidate output remains stable for configured delay.

## Expression language (Python-style DSL)

The same DSL is used in `check`, `value`, and places where time/value is computed.

Authoring rule:
- Use textual expressions with normal parentheses `(` `)` only.
- Do not use JSON expression arrays for new rules.

## Core operators

- Logical: `and`, `or`, `not`
- Comparison: `=`, `!=`, `<>`, `>`, `<`, `>=`, `<=`
- Arithmetic: `+`, `-`

Parentheses control grouping precedence.

## EBNF grammar

The following EBNF defines the parser contract for expression fields.

```ebnf
fieldScript        = { declarationLine , lineBreak } , resultExpression ;

declarationLine    = identifier , ws? , "=" , ws? , mapLiteral ;
resultExpression   = expression ;

expression         = orExpr ;
orExpr             = andExpr , { ws , "or" , ws , andExpr } ;
andExpr            = compareExpr , { ws , "and" , ws , compareExpr } ;
compareExpr        = addExpr , [ ws? , comparator , ws? , addExpr ] ;
addExpr            = unaryExpr , { ws? , addOp , ws? , unaryExpr } ;
unaryExpr          = [ "not" , ws ] , primary ;

primary            = mapCall
                   | ifCall
                   | mapLiteral
                   | "(" , ws? , expression , ws? , ")"
                   | valueToken ;

ifCall             = "if" , ws? , "(" , ws? , expression , ws? , "," , ws? , expression , ws? , "," , ws? , expression , ws? , ")" ;
mapCall            = identifier , ws? , "(" , ws? , expression , ws? , ")" ;

mapLiteral         = "(" , ws? , mapEntry , { ws? , "," , ws? , mapEntry } , ws? , ")" ;
mapEntry           = mapKey , ws? , ":" , ws? , expression ;
mapKey             = "default" | valueToken ;

valueToken         = quotedString | number | identifier | variableRef ;
variableRef        = ("$" | "/" | identifierStart) , { variableChar } , "/" , { variableChar } ;
identifier         = identifierStart , { identifierChar } ;

comparator         = "=" | "!=" | "<>" | ">" | "<" | ">=" | "<=" ;
addOp              = "+" | "-" ;

quotedString       = '"' , { stringChar } , '"'
                   | "'" , { stringChar } , "'" ;

number             = [ "-" ] , digit , { digit } , [ "." , digit , { digit } ] ;

identifierStart    = letter | "_" ;
identifierChar     = letter | digit | "_" ;
variableChar       = letter | digit | "_" | "-" | "." | "$" | "/" | " " ;

lineBreak          = "\n" | "\r\n" ;
ws                 = { " " | "\t" } ;

letter             = "A".."Z" | "a".."z" ;
digit              = "0".."9" ;
stringChar         = ? any character except unescaped quote and line break ? ;
```

Grammar notes:
- `fieldScript` applies to `check`, `value`, and computed `time` expressions.
- `valueToken` allows unquoted symbolic tokens (`awake`, `off`, `Mon`) and topic-like variable references.
- `variableRef` is recognized by containing at least one `/`.
- `mapEntry` with key `default` is mandatory in semantic validation, even if grammar accepts map literals without it.

## Map declarations (replacement for switch)

Map declaration syntax:
- `name = (key1: value1, key2: value2, default: valueDefault)`

Map invocation syntax:
- `name(selectorExpression)`

Semantics:
- Evaluate `selectorExpression`.
- If selector matches a declared key, return mapped value.
- Else return `default` value.
- `default` key is mandatory for deterministic behavior.

Example:
- `presence = (1: awake, on: awake, awake: awake, sleeping: sleeping, default: absent)`
- `$MONITOR/presence != presence($MONITOR/presence/set)`

## Multi-line field script format

Each expression field may contain:
1. Zero or more declaration lines.
2. One final result expression line.

Execution order:
- Declaration lines are evaluated top-down.
- The final line is the field result.

Example (`check` field):
- `presence = (1: awake, on: awake, awake: awake, sleeping: sleeping, default: absent)`
- `$MONITOR/presence != presence($MONITOR/presence/set)`

## If semantics

Conditional expression syntax:
- `if(condition, trueValue, falseValue)`

Semantics are equivalent to legacy `if` node behavior.

## String literal and quoting rules

- Quotes are optional for tokens that contain only safe identifier characters and no whitespace.
- Quotes are required when value contains spaces or separator characters.
- Unquoted topic-like tokens (for example `/time`, `$MONITOR/presence`) are treated as variable references.
- Literal text values must be quoted when ambiguous.

## Semantic equivalence requirement

The Python-style DSL must keep the same evaluation semantics as legacy rules functionality:
- same boolean/arithmetic outcomes,
- same variable resolution behavior,
- same switch/default behavior,
- same time arithmetic behavior.

Time arithmetic behavior:
- Subtracting numeric value from a time expression means subtracting minutes.

## Variable model

## Variable naming

- Any string containing / is treated as variable/topic reference by rule logic.

## Default derived variables

Automation must provide these derived variables:
- /time
- /weekday
- /sunrise
- /sunset
- /civildawn
- /civildusk
- /nauticaldawn
- /nauticaldusk
- /astronomicaldawn
- /astronomicaldusk

Sun variable source behavior:
- /sunrise, /sunset and twilight variables are delegated to the external sun calculation function set.
- Automation itself does not implement astronomical formulas.
- Automation itself does not define special-case fallback values for locations/dates where sun calculation may be undefined or non-Date; behavior is delegated to the integrated sun provider output.

Delivery boundary requirement:
- The sun provider is a mandatory part of the Automation implementation scope.
- It may be implemented as a separate internal module/subdirectory, but it must be delivered and wired together with Automation.
- Automation is not considered complete without an integrated sun provider implementation.

## Dynamic variables from incoming messages

- For each non-control incoming message, set variable(topic)=value.
- Outgoing own messages are also reflected into variable map after each processing cycle.

## Presence bootstrap

On component initialization:
- Set variable(presenceTopic) = initial.

## Event history model

Automation stores two event classes.

1. Motion events
- Stored as ordered list of { topic, timestamp, id }.
- Max list length: 100.
- Trim hysteresis: when length exceeds max, remove oldest 20 entries at once.

2. Non-motion events
- Stored as map topic -> true.
- Represents one-cycle triggers.

Event ingestion rule:
- If incoming message value == 0, ignore event entirely (no motion and no non-motion insertion).

Latest event extraction:
- getLatestEvents(relatedMotionTimespanInSeconds=5) returns:
  - timestamp: latest motion timestamp or 0
  - motions: map topic -> timestamp for motions within related window
  - nonMotions: current non-motion map
  - displayString: log-friendly concatenation

Non-motion lifecycle:
- nonMotions are cleared at end of each processTasks cycle.

## Runtime control topics

## Rule management channel

Accepted command topic pattern:
- $MONITOR/automation/rules/<ruleName>/set

Command payload semantics:
- delete: remove rule named ruleName.
- otherwise payload must be JSON object string representing rule.

Set/update behavior:
- Parse JSON payload.
- If payload has no name, set payload.name = ruleName.
- Call rules.setRule(payload).
- Emit ack message with serialized rule JSON.
- If setRule validation failed, emit additional ack with payload invalid rule.

Delete behavior:
- Call rules.deleteRule(ruleName).
- Emit ack payload deleted.

All rule-management ack messages use qos 1.

## Simulation channel

Supported topics:
- $MONITOR/simulation/date
- $MONITOR/simulation/end
- $MONITOR/simulation/<domainTopic>

Behavior:
1. Receiving simulation/date when not already in simulation sets simulation mode true.
2. simulation/date payload is parsed as time-of-day date and used to run processTasks(simulationDate, true).
3. simulation/end sets simulation mode false.
4. Other simulation topics are mapped by removing prefix $MONITOR/simulation/ and then treated as normal incoming domain events in simulation context.
5. For mapped simulation messages, reason[0].timestamp is parsed and normalized to ISO string before processing.

Safety gate:
- processTasks(date, simulationFlag) must return empty messages when simulationFlag != internal simulation mode.

## Core processing behavior

## handleMessage algorithm

For each incoming message:
1. Initialize responseMessages = [].
2. Route by topic class:
   - simulation control -> simulation handler
   - automation rule control -> rule management handler and responseMessages
   - otherwise normal event ingestion
3. After message-specific handling, execute processTasks(now, false) when in normal mode.
4. Return concatenation of ruleMessages + responseMessages.
5. Any exception is caught and logged; return best-effort message list.

## processTasks algorithm

1. Enforce simulation mode match guard.
2. Set rule processor date context.
3. Snapshot recentEvents from history.
4. Iterate all rules in rule set order.
5. For each rule:
   - Evaluate durationWithoutMovement gate:
     - if configured, require inactivity threshold satisfied.
   - Determine activeMotion map:
     - if durationWithoutMovementInMinutes is undefined and latest motion is older than 60s, activeMotion becomes empty map.
     - otherwise activeMotion = recentEvents.motions.
   - Execute rule check through rules processor with activeMotion and nonMotions.
   - Merge returned messages and usedVariables.
   - On exception: invalidate faulty rule, log error, continue with next rule.
6. If result has messages, write each output message topic/value back into variable map.
7. Log undefined variables once per variable name.
8. Clear non-motion events.
9. Return { messages, usedVariables }.

## Trigger and dedup behavior

Behavior inherited from rules engine contract and required for parity:
- Rule outputs are change-triggered by default.
- Identical topic/value outputs are suppressed if unchanged.
- When condition later makes same output inactive, suppression history is updated so the same output can be emitted again on a future reactivation.
- cooldownInSeconds overrides strict one-shot suppression and allows periodic resend while rule remains active.
- delayInSeconds requires stable repeated candidate output before actual emission.

## Rule variable discovery

Before returning subscriptions, Automation performs rule check pass to collect variable references.

Collected variable topics become subscription candidates if not already matched by motion topic patterns.

## Data model summary

Internal component state includes:
- presenceTopic
- subscribeQoS
- motionTopics[]
- filestoreOptions
- history (EventHistory)
- processRule context object
- rules object (parsed/executable rule tree)
- simulation flag
- usedVariables memory for one-time undefined logging

## Persistence

Persisted content:
- Entire current rule tree only.

Persistence target:
- External file-store HTTP service.

Write trigger:
- Every runtime rule add/update/delete command.

Read trigger:
- prepare startup.

If file-store read fails or disabled, fallback to local files.

No persistence for:
- variable state,
- motion history,
- non-motion history,
- simulation state.

## Error handling

Expected error classes and handling:

1. Rule evaluation runtime error
- Action: invalidate offending rule and continue processing remaining rules.

2. Top-level message processing error
- Action: catch and log; avoid component crash.

3. File-store read error
- Action: log and continue runtime with current/empty in-memory rules; no local file fallback.

4. File-store write error
- Action: log and continue runtime.

5. Rule update payload parse/validation error
- Action: emit invalid rule acknowledgment (best effort) and keep runtime alive.

6. Local file parse/read error while loading rule files
- Action: log; resulting rule tree may be partial/empty based on successful files.

## Logging capability parity

Automation client logging capabilities must be parity-compatible with legacy automation client behavior for inbound and outbound message tracing.

Mandatory implementation requirements:
- Incoming message logging can be enabled/disabled at runtime via `automation.logIncomingMessages` INI setting.
- Outgoing message logging can be enabled/disabled at runtime via `automation.logOutgoingMessages` INI setting.
- Both settings default to `false` when omitted.
- When enabled, logging must include topic, value, qos, and retain fields for each message direction.

## Deterministic compatibility requirements

For a reimplementation to be considered parity-correct, these requirements are mandatory:

1. Topic namespace mapping from legacy $SYS to $MONITOR must be applied consistently for all Automation control paths.
2. Rule evaluation must run both:
   - after every incoming message,
  - and on periodic schedule with intervalInSeconds seconds.
3. MQTT client runtime must support periodic callback registration for processTasks publishing; if missing, implementation must add it.
4. Event history sizing and trimming constants must match:
   - max 100 motion entries,
   - trim 20 oldest on overflow.
5. Related motion window default must be 5 seconds.
6. Motion-staleness threshold for event-only processing must be 60 seconds when durationWithoutMovementInMinutes is not set.
7. Non-motion events must be single-cycle and cleared after each processTasks run.
8. Incoming messages with value 0 must not be added to event history.
9. Rule management ack messages must use qos 1.
10. Startup rule-source is file-store-only when `filestore.use=true`; local fallback is retired.
11. processTasks must enforce simulation mode match guard.
12. Incoming/outgoing logging toggles must exist with defaults disabled and must log message topic/value/qos/retain when enabled.

## Architectural notes

- Automation uses shared cross-cutting contracts from:
  - SPEC-IMqttComponent
  - SPEC-message
- Rule DSL and event semantics described here remain component-local because they are not yet specified as shared multi-component contract.
- If another YAHA component adopts the same rule DSL, extract this DSL section into dedicated cross-cutting SPEC-rules-engine.

## Open questions

- None inside current code-defined scope.