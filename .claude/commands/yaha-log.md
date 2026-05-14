Skill for creating and maintaining the YAHA reimplementation journal.
Caveman language. No format chars. No unneeded words.

## Purpose

Keep a running log of everything that happens during the YAHA reimplementation.
Automatic. No user request needed. AI writes to it after every significant action.

## Log file

spec/yaha/JOURNAL.md

Create if not exists. Append only. Never rewrite existing entries.

## When to write

After EVERY action that belongs to one of these types:

- ARTIFACT: any file created, updated, deleted, merged
- MILESTONE: something functionally complete (spec done, impl plan done, impl step done)
- DECISION: a rule, convention, or design choice was established or changed
- CORRECTION: a mistake was found and fixed (wrong spec, wrong structure, etc.)
- INSIGHT: user pointed out something that changed how we approach a topic

Do not write for trivial edits (typo fixes, formatting). Write for anything with architectural or content significance.

## Entry format

Entries group under a date heading. Within a date, entries are in order of occurrence.

```
## YYYY-MM-DD

### [TYPE] short title (max 10 words)
One to three sentences. What happened. Why. What changed as a result.
Reference affected files if relevant.
```

TYPE is one of: ARTIFACT, MILESTONE, DECISION, CORRECTION, INSIGHT

## How to write well

ARTIFACT — name the file, say created/updated/deleted/merged, say why in one sentence.
MILESTONE — name what is complete, reference the plan step or component.
DECISION — state the rule, state the reason. If it came from user input, say so.
CORRECTION — name what was wrong, what is right now, what was affected.
INSIGHT — name who raised it (user or AI), what was discovered, what was created or changed as a result.

## Integration with other skills

yaha-spec.md: after creating or updating any spec file, write journal entries for all artifacts and any decisions or insights that arose.
Any implementation skill: after completing a plan step, write a MILESTONE entry. After creating/updating code files, write ARTIFACT entries.

## Example entries

### [ARTIFACT] spec/yaha/SPEC-IMqttComponent.md created
Identified as cross-cutting concern during MessageStore spec work. Defines the pure virtual interface between MQTT client and any component. Applies to all future components.

### [CORRECTION] SPEC-messagetree.md deleted, content merged into messagestore
MessageTree is only used by MessageStore — not a cross-cutting concern. Standalone spec was wrong. All content moved into SPEC-messagestore.md under Data model section.

### [DECISION] $SYS topics renamed to $MONITOR
MQTT spec reserves $SYS for broker use only. On user instruction: all legacy $SYS/... topics become $MONITOR/... in new specs. Skill and SPEC-messagestore.md updated.

### [INSIGHT] Message identified as central shared data type
User prompted review. Found that Message (topic, value, reason, qos, retain) is used identically across valueservice, raspberry, zwave, pushover, serialdevice, and more. Dedicated SPEC-message.md created. Skill updated to check shared data formats, not just interfaces.
