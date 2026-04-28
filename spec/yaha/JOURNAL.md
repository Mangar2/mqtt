# YAHA Reimplementation Journal

## 2026-04-28

### [ARTIFACT] .claude/commands/yaha-spec.md created
New skill created to guide step-by-step specification of YAHA components being reimplemented in C++. Defines workflow: read legacy code, identify cross-cutting concerns, write spec, review, output summary.

### [ARTIFACT] spec/yaha/SPEC-IMqttComponent.md created
Identified as cross-cutting concern during MessageStore spec work. Defines the pure virtual interface (`getSubscriptions`, `handleMessage`, `setPublishCallback`) that decouples any component from the MQTT transport layer. Applies to all future components.

### [ARTIFACT] spec/yaha/SPEC-messagetree.md created
Created as standalone spec for the internal MessageTree data structure used by MessageStore.

### [ARTIFACT] spec/yaha/SPEC-messagestore.md created
First component spec. Covers MQTT subscriptions, HTTP query interface, message tree data model, persistence, configuration, and lifecycle.

### [CORRECTION] SPEC-messagetree.md deleted, content merged into SPEC-messagestore.md
MessageTree is only used by MessageStore — not a cross-cutting concern. A standalone spec was the wrong structure. On user feedback, all content moved into SPEC-messagestore.md (Data model, Configuration, Architectural notes, Open questions sections). Skill updated: dedicated spec only for concerns used by more than one component.

### [DECISION] $SYS topics renamed to $MONITORING
MQTT specification reserves the $SYS prefix for broker-originated messages. Components must never publish or subscribe to $SYS topics. On user instruction: all legacy $SYS/... topics become $MONITORING/... in new specs. SPEC-messagestore.md corrected at all three occurrences. Skill section "MQTT topic conventions" added.

### [INSIGHT] Message identified as central shared data type
User prompted a review of whether the Message format deserves its own spec. Cross-check across all legacy components (valueservice, raspberry, zwave, pushover, serialdevice, and others) confirmed that `{topic, value, reason, qos, retain}` with the ReasonEntry chain is the universal data carrier of the system. SPEC-message.md created. Skill Step 2 updated to explicitly check shared data formats (not just interfaces) as a cross-cutting concern trigger.

### [ARTIFACT] .claude/commands/yaha-log.md created
New skill created to define when and how the AI maintains the journal. Specifies entry types (ARTIFACT, MILESTONE, DECISION, CORRECTION, INSIGHT), format, and integration with other yaha skills.

### [ARTIFACT] spec/yaha/JOURNAL.md created
This file. Retroactively populated with all events from the initial session.
