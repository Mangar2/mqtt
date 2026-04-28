# Automation

This is one of the core modules of yaha, the automation. Use it to automate your processes. The automation is "event-driven". There are many included features to make it simple to define rules - still it is not always easy to understand why and what for.
Please - this time - read the manual from the beginning

## Content

- [Automation](#automation)
  - [Content](#content)
  - [Installation](#installation)
  - [Configuration](#configuration)
    - [What you have to provide](#what-you-have-to-provide)
  - [Rule processing](#rule-processing)
    - [Rule tree](#rule-tree)
    - [rule format](#rule-format)
    - [topic](#topic)
    - [value](#value)
    - [time](#time)
    - [Usecase example, switch off at fixed time](#usecase-example-switch-off-at-fixed-time)
    - [calculations](#calculations)
      - [variables](#variables)
      - [calculation tree format](#calculation-tree-format)
    - [check](#check)
    - [use-case switch outdoor lights](#use-case-switch-outdoor-lights)
    - [cooldownInSeconds, how rules trigger](#cooldowninseconds-how-rules-trigger)
    - [delayInSeconds](#delayInSeconds)
    - [usecase switch on for one hour](#usecase-switch-on-for-one-hour)
    - [duration, limit time](#duration-limit-time)
    - [qos](#qos)
    - [oneOf, allOf](#oneof-allof)
    - [usecase presence status](#usecase-presence-status)
    - [usecase safe energy](#usecase-safe-energy)
    - [noneOf](#noneof)
    - [allow](#allow)
    - [durationWithoutMovementInMinutes](#durationwithoutmovementinminutes)
    - [usecase alarm on movement if absent](#usecase-alarm-on-movement-if-absent)
    - [usecase livingroom roller shutter](#usecase-livingroom-roller-shutter)
      - [roller up in the morning](#roller-up-in-the-morning)
      - [roller down for sun protection](#roller-down-for-sun-protection)
      - [roller move up again at afternoon](#roller-move-up-again-at-afternoon)
      - [roller moves down at sunset](#roller-moves-down-at-sunset)

## Installation

Do not install it directly, instead install it with npm @mangar2/runservices

## Configuration

Use the runservices service to use the automation module and write the configuraiton in the configuration files used by runservices. The configuration must be provided in JSON with the follow Schema

```JSON
{
    "title": "Motion configuration",
    "type": "object",
    "properties": {
        "motionTopics": {
            "description": "Subscription list to receive all relevant events/movements",
            "type": "array",
            "items": { "type": "string" }
        },
        "subscribeQoS": { "enum": [0, 1, 2] },
        "presenceTopic": {
            "description": "Topic holding the presence state",
            "type": "string"
        },
        "rules": {
            "description": "Filename(s) to a rule file (type string: single, type array:0..n)",
            "type": ["string", "array"],
            "items": { "type": "string" }
        },
        "intervalInSeconds": {
            "description": "Iinterval between checks for events based on rules. Additionally all rules will be checked on every receive Message",
            "type": "integer"

        },
        "longitude": { "type": "number" },
        "latitude": { "type": "number" },
        "additionalProperties": false
    },
    "required": ["motionTopics", "longitude", "latitude"]
}
```

The default configuration is:

```JavaScript
const defaultConfiguration = {
    motionTopics: ['+/+/+/motion sensor/detection state', '$SYS/presence/set'],
    presenceTopic: '$SYS/presence',
    rules: ['rules.json'],
    intervalInSeconds: 60,
    subscribeQoS: 1
}
```

- motionTopis: An array of topics containing events (usually motion events)
- presenceTopic: the topic you use to signal the presence state
- rules: the list of rule files
- intervalInSeconds: the amount of seconds between checking the rules for new actions. Note: rules are checked additionally on any received message
- subscribeQoS: Qualiy of service used to subscribe for any topic used in the automation rules

### What you have to provide

The only element you **have to** provide additionally is longitude, latitude: the geo-coordinates of your location to calculate sunset/sunrise in decimal degrees (DD). As example the London Tower Bridge has the coorinates 51.505153, -0.075664

I recommend to provide additionally a list of rule files - use more than one to structure your rules if you will have a larger set of them. You may keep everything else as default. A configuration example is:

```JSON
{
    "production": {
        "automation": {
            "longitude": -0.075664,
            "latitude": 51.505554,
            "rules": [
                "C:\\yourpath\\motionrules.json",
                "C:\\yourpath\\automationrules.json",
                "C:\\yourpath\\alertrules.json"
            ]
        }
    }
}
```

## Rule processing

The automation is based on "rules" set in a JSON file. There is no easy user-interface to define them (maybe it will come later).

### Rule tree

You are free to put the rules in a "tree" on any level in JSON-Objects. The parser will scan for the property name "rules" and will take everything that follows as rule. The "rules" property **must** be the last organizational level in the rule tree.
Examples:

```JSON
{
    "rules": {
        "myRuleName1": {},
        "myRuleName2": {}
    }
}
```

or

```JSON
{
    "ground": {
        "rules": {
            "myRuleName": {}
        }
    },
    "first": {
        "myRuleName": {}
    }
}
```

or

```JSON
{
    "ground": {
        "livingroom": {
            "multimedia": {
                "rules": {
                    "myRuleName": {}
                }
            }
        }
    }
}
```

### rule format

A rule is a JSON object with the following members, mandatory is only "topic".

```JSON
{
    "rules": {
        "myRuleName": {
            "time": "",
            "duration": "",
            "check": "",
            "value": "",
            "topic": "",
            "cooldownInSeconds": 60,
            "delayInSeconds": 0,
            "qos": "",
            "allOf": "",
            "anyOf": "",
            "noneOf": "",
            "allow": "",
            "durationWithoutMovementInMinutes": 5
        }
    }
}
```

The simplest possible move is (both following rules are doing the same, it is an alternative way to write it):

```JSON
{
    "rules": {
        "simpleRule1": {
            "value": "on",
            "topic": "ground/livingroom/light/set"
        },
        "simpleRule2": {
            "topic": {"ground/livingroom/light/set": "on"}
        }
    }
}
```

What does this rule do? It will send a message with topic "ground/livingroom/light" and value on **once** and never again. We need to add something to make it sensful.

### topic

The topic is mandatory. It is a unique id for the device or the action that is triggered by the rule. The topic will be send to the central message broker and then forewarded to the devices which subscribed to the topic.

For example the topic ground/livingroom/light could be send to a device switching the light in the livingroom on or off. The job for the automation is just sending a message. Acting must be done by the actors. This makes yaha highly flexible. We can add any actor we like without having to change the automation module

There are three ways to format topics. One rule can trigger many messages with different topics and values:

```JSON
{
    "rules": {
        "switchLivingroomLightOn": {
            "value": "on",
            "topic": "ground/livingroom/light/set"
        },
        "switchLivingroomLightAndFloorLightOn": {
            "value": "on",
            "topic": ["ground/livingroom/light/set", "ground/floor/light/set"]
        },
        "switchLivingroomLightOnAndFloorLightOff": {
            "topic": { "ground/livingroom/light/set": "on", "ground/floor/light/set": "off" }
        }
    }
}
```

### value

The value is the state of the device to set. I recommend to use "set" topics to change a state and the same topic without set to signal a state. You can provide any value you like - it must be interpreted by the actors. (Note the topic with and without "set" is only a recommendation. You are completely free to define your own topics or use the topics your actor requires).
Using the "set" principle, the automation will usually use "set" topics while the devices will send "status" topics without set to inform about successful state changes.

```JSON
{
    "rules": {
        "switchLivingroomLightOn": {
            "topic": "ground/livingroom/light/set",
            "value": "on"
        },
        "signalThatLivingroomLightIsOn": {
            "topic": { "ground/livingroom/light": "on"}
        }
    }
}
```

Why have "value", if you can just place the value as property value in the topic? Sometimes it is more clear to bring it separately. For example for many topics with the same value or if the value is calculated by a complex term. But value is not needed, it is always possible to place any value in the topic property

### time

The time property defines the start time of the rule

Time requires a "time of day" string formatted as HH:MM:SS or HH:MM. Example 9:00 or 09:00 or 09:00:00 are all setting the rule start time to 9 am.

The following example will switch a light on at 20:00 in the evening and switch it off at 6:00 in the morning.

```JSON
{
    "rules": {
        "switchGardenLightOn": {
            "time": "20:00",
            "topic": { "outdoor/light/set": "on"}
        },
        "switchGardenLightOn": {
            "time": "6:00",
            "topic": { "outdoor/light/set": "off"}
        }
    }
}
```

### Usecase example, switch off at fixed time

A nightly rule can switch off anything you forgot to switch of yourself

```JSON
{
    "rules": {
        "switchOfNigh": {
            "time": "3:00",
            "value": "off",
            "topic": ["garden/light", "livingroom/multimedia", "floor/heating"]
        }
    }
}
```

### calculations

Rule values supports a calculation tree to calculate values. The decision tree can be used in many places in the rule definition - for example to calculate a value

#### variables

When calculating values we need variables. A string with a "/" in it is a variable. The automation module can subscribe to receive topics and will then automatically store them as variables. This allows for example to act on temperature change.

The following default variables are automatically set, you need to set longitude and latitude of your location in the configuration file to get proper values for sun related times:

- /time current time of day
- /weekday current day in the week (0 for sunday, 1 for monday, ...)
- /sunrise time of sunrise (today)
- /sunset time of sunset
- /civildawn, /civildusk (please google)
- /nauticaldawn, /nauticaldusk (please goolgle)
- /astronomicaldawn, /astronomicaldusk (please goolge)

#### calculation tree format

The calculation tree has a near lisp syntax. Lisp is a programming language. Suported are

- ["and", term1, term2, ...]
- ["or", term1, term2, ...]
- [comperator, term1, term2] with comperator =, !=, <>, >, <, >=, <=
- [operator, term1, term2] with operator = + or -
- ["if", condition, trueValue, falseValue]
- ["switch", term, { value1: term1, value2: term2, ..., default: termDefault }]

every term can be calulated as well with the same structure.

Example:
The following example will switch the light on between 20:00 and 6:00 and switch it off between 6:00 and 20:00. It does the same as in the example of the "time" section. But I think the example in the time secition is easier to read - this one here could be prefered by programmers.

```JSON
{
    "rules": {
        "switchGardenLightOn": {
            "time": "20:00",
            "value": [
                "if",
                ["or", [">=", "/time", "20:00"], ["<=", "/time", "6:00"]],
                "on",
                "off"
            ],
            "topic": "outdoor/light/set"
        }
    }
}
```

### check

The check property adds an additional check to the a rule that must evaluate to true to trigger the rule. The "calculation" section how the check decision can be defined.

Example:
This example will switch on a light in the morning at 6:00 if the sun has not been rised. (current time is less than sunrise)

```JSON
{
    "rules": {
        "switchLightOnMorning": {
            "time": "6:00",
            "check": ["<", "/time", "/sunrise"],
            "topic": { "outdoor/light/set": "on" }
        }
    }
}
```

### use-case switch outdoor lights

The already described possibilities are sufficient to create the first real use-case. Handling light outdoor.

```JSON
{
    "rules": {
        "switchOnMorning": {
            "time": "6:00",
            "check": ["<", "/time", ["-", "/sunrise", 10]],
            "topic": { "outdoor/light/set": "on" }
        },
        "switchOffSunrise": {
            "check": [">=", "/time", "/sunrise"],
            "topic": { "outdoor/light/set": "off" }
        },
        "switchOnSunset": {
            "check": ["and" [">=", "/time", "/sunset"] ["<=", "/time", "22:50"]],
            "topic": { "outdoor/light/set": "on" }
        },
        "switchOffNight": {
            "time": "23:00",
            "topic": { "outdoor/light/set": "off" }
        }
    }
}
```

Explained:

**switchOnMorning** switches the light on, if it is 6:00 or later AND current time is at least 10 minutes before sunrise. Please not that subtracting numbers from times is interpreted as subtracting minutes. This is the typical usecase for automation

**switchOffSunrise** switches the light off on sunrise. Please note that we have a 10 minutes differens between the first rule - the light will not switch on for less than 10 minutes.

**switchOnSunset** switches the light on again on sunset - if it is not later than 22:50

**switchOffNight** switches the light off at 23:00

Please note that you may switch several lights by adding topics or by having multiple actors subscribing to "outdoor/light/set" - it is up to you to design this. I prefer to have one topic per light

### cooldownInSeconds, how rules trigger

The rule triggering has some intelligence built in. It will only trigger on changes.

Example:

```JSON
{
    "rules": {
        "switchOnMorning": {
            "time": "6:00",
            "check": ["<", "/time", ["-", "/sunrise", 10]],
            "topic": { "outdoor/light/set": "on" }
        }
    }
}
```

switchOnMorning will only trigger once after time is later than 6:00 and the sun rised. Rules only trigger once with the same value. It will trigger again, if the value changes, example:

```JSON
{
    "rules": {
        "switchOnMorning": {
            "time": "6:00",
            "value": ["if", ["<", "/time", "/sunrise"], "on", "off"],
            "topic": "outdoor/light/set"
        }
    }
}
```

This rule will trigger after 6:00 before sunrise to set it on and after sunrise to set it off

This behaviour can be changed by adding the "cooldownInSeconds". The following rule will trigger every 10 seconds starting at 6:00 as long as the current time is before sunrise. Do not set "cooldownInSeconds" to values lower than 60 seconds. The automation is checking rules only every minute or whenever a message is received (the timing can be changed in the configuration file)

```JSON
{
    "rules": {
        "switchOnMorning": {
            "time": "6:00",
            "cooldownInSeconds": 60,
            "check": ["<", "/time", "/sunrise"],
            "topic": { "outdoor/light/set": "on" }
        }
    }
}
```

### usecase switch on for one hour

This example shows how to create rules switching devices on for one hour in the morning and in the evening

Four rule solution, very simple (simple is good).

```JSON
{
    "rules": {
        "switchOnMorning": {
            "time": "6:00",
            "topic": { "anything/set": "on" }
        },
        "switchOffMorning": {
            "time": "7:00",
            "topic": { "anything/set": "off" }
        },
        "switchOnEvening": {
            "time": "20:00",
            "topic": { "anything/set": "on" }
        },
        "switchOffEvening": {
            "time": "21:00",
            "topic": { "anything/set": "off" }
        }
    }
}
```

The following works too:

```JSON
{
    "rules": {
        "switchOn": {
            "value": ["if",
                ["and", [">=", "/time", "6:00"], ["<=", "/time", "7:00"]],
                "on", "off"],
            "topic": "anything/set"
        },
        "switchOn": {
            "value": ["if",
                ["and", [">=", "/time", "20:00"], ["<=", "/time", "21:00"]],
                "on", "off"],
            "topic": "anything/set"
        }
    }
}
```

Or all in one, the "if" version can be combined:

```JSON
{
    "rules": {
        "switch": {
            "value": ["if",
                ["or"
                    ["and", [">=", "/time", "6:00"], ["<=", "/time", "7:00"]],
                    ["and", [">=", "/time", "20:00"], ["<=", "/time", "21:00"]]
                ]
                "on", "off"],
            "topic": "anything/set"
        }
    }
}
```

### delayInSeconds

Most rules shall trigger immediately. But in some cases, a situation should be stable for some time, before the message is sent. DelayInSeconds is the amount of seconds required for a rule to create an identical message (topic/value) before the message is sent 

Example:

```JSON
{
    "rules": {
        "standby": {
            "check": ["<", "/power", 10],
            "delayInSeconds": 60,
            "topic": { "powerswitch/set": "off" }
        }
    }
}
```

The power must be below 10 for at least 60 seconds, before power is switched off.

### duration, limit time

The timespan of the rule can be limited with the duration property. Duration needs a time property to be relevant.
The duration value must be formatted as HH:MM or H:MM. **The duration has a default value of six hours if not specified**

Example:

```JSON
{
    "rules": {
        "switchOnEvening": {
            "time": "18:00",
            "duration": "4:00",
            "check": [">", "/time", "/sunset"],
            "topic": { "outdoor/light/set": "on" }
        }
    }
}
```

This rule now switches the the light on starting at 18:00 after sunset but only if sunset was before 18:00 + 4:00 = 22:00

### qos

qos or quality of service defines the quality of service whith which the message is delivered

- 0, the message is just sent once - there is no acknowledge that the message has been delivered. This is the fastest way to send messages, but messages can get lost
- 1, the message is sent at least once - the message is send again and again until an acknowledge is received
- 2, the message is sent exactly once - checked by a two step handshake. This takes the longes time and produces a large amount of communication

**Default is qos = 1** - leave it in most cases, if you are in a local network. Use qos = 0, if the message is sent very often and it is not important to get all of them. Use qos = 2, if the message is really importend like a water leakage alarm.

### oneOf, allOf

These properties (together with noneOf and allow) are used for event driven automation rules like movements.

*oneOf* requires one of the topics in the array, *allOf* requires all topics in the array.

This functionality works completely different than the rules shown before. Here the rule triggers **every time** it detects an action (use cooldownInSeconds to limit it)

The following rule will send a presence message if a movement in the floor occured:

```JSON
{
    "rules": {
        "switchOn": {
            "oneOf": ["floor/movement"],
            "topic": { "$SYS/presence": "awake" }
        }
    }
}
```

The following rule triggers if a movement in the floor and in the kitchen occured at (nearly) the same time. "Nearly same time" is in 5 seconds (configurabe in the configuration file).

The following rule will trigger if someone walks from kitchen to floor or from floor to kitchen

```JSON
{
    "rules": {
        "switchOn": {
            "allOf": ["floor/movement", "kitchen/movement"],
            "topic": { "do/something": "left or entered kitchen" }
        }
    }
}
```

### usecase presence status

Having a presence status will bring you to an advanced level of automation. Many rules profits from the knowlege of presence. Use for example the values "absent", "present", "sleeping".

The following example will set the status to presence, if there is a motion detected on the ground-floor.

```JSON
{
    "rules": {
        "wakeup": {
            "check": ["=", "system/presence", "sleeping"],
            "allOf": ["floor/motion"],
            "topic": { "system/presence": "present" }
        },
        "comingHome": {
            "check": ["=", "system/presence", "absent"],
            "allOf": ["floor/motion"],
            "topic": { "system/presence": "present" }
        }
    }
}
```

### usecase safe energy

Many electronic devices only needs to run if somebody is present ant not sleeping - for example a smart speaker. With a presence status it can be controlled easily.

```JSON
{
    "rules": {
        "onIfPresent": {
            "value": [
                "switch",
                "system/presence",
                { "present": "on", "default": "off" }
            ],
            "topic": [
                "livingroom/multimedia/set",
                "study/smartspeaker/set"
            ]
        }
    }
}
```

### noneOf

Use noneOf to prevent rule triggering, if an event occured. A wakeup rule can be hindered to trigger on movement in the bathroom.

This may prevent setting your home to "present" if you are only going to the bathroom at night.

```JSON
{
    "rules": {
        "wakeup": {
            "check": ["=", "system/presence", "sleeping"],
            "anyOf": ["ground/floor/motion"],
            "noneOf": ["first/bathroom/motion", "first/floor/motion"],
            "topic": { "system/presence": "present" }
        }
    }
}
```

### allow

Here you may add allowed events - all non allowed events are then denied. Use it rarely (better use "noneOf" and list all rules that are not allowed), I try to get rid of it - it will depricate.

### durationWithoutMovementInMinutes

Use this to detect absence or sleeping states - no movement for quite some time with the last movement at the front door or at the floor of the sleeping room.

```JSON
{
    "rules": {
        "sleeping": {
            "check": ["=", "system/presence", "present"],
            "anyOf": ["first/floor/motion"],
            "durationWithoutMovementInMinutes": 15,
            "topic": { "system/presence": "sleeping" }
        },
        "absence": {
            "check": ["=", "system/presence", "present"],
            "allOf": ["ground/floor/motion"],
            "durationWithoutMovementInMinutes": 30,
            "topic": { "system/presence": "absent" }
        }
    }
}
```

### usecase alarm on movement if absent

Having presence/absence detected you may add the ability to send an alarm to your smartphone. Yaha supports the "pushover" app for this task

```JSON
{
    "rules": {
        "alarm": {
            "check": ["=", "system/presence", "absent"],
            "anyOf": ["+/+/motion"],
            "topic": { "system/alarm": "motion" }
        }
    }
}
```

### usecase livingroom roller shutter

This is a real world example. It shows many features of the automation module. You need to have a presence topic to use it.

Note that you do not need to subscribe for the topics needed (like indoor and outdoor temperature). The system detects that you are using these topic and thus will automatically subscribe for them (see mqttbroker documentation if you did not understand this note).

#### roller up in the morning

Imagine to have two rollers in a livingroom (south and east). When shall the roller move up? Surely not too early while sleeping, but when you are awake - and sun rised.

```JSON
{
    "rules": {
        "UpMorning": {
            "time": ["switch", "system/presence", {"absent": "8:00", "awake": "6:00", "sleeping": "10:00"}],
            "check": [">=", "/time", ["-", "/sunrise", 10]],
            "topic": {
                "shutter/east/set": 0,
                "shutter/west/set": 0
            }
        }
    }
}
```

This rule solves many cases in a short simple rule.

- Does not move up too early to not wake neighours ("awake": "6:00")
- Does not move up while you are sleeping, only if you are sleeping too long ("sleeping": "10:00")
- Does move up at "normal" time while not at home (for example in holidays)
- Does not move up if the sun did not rise
- Moves two shutter roller (west and east)
- Reacts at the right time (if you awake at 7:33, it will move up at 7:33 and not forget to move up - because 6:00 already passed)
- Moves up only once, even if you wake up and then get back sleeping

#### roller down for sun protection

```JSON
{
    "rules": {
        "DownCooling": {
            "time": ["switch", "system/presence", {"absent": "9:30", "awake": "12:00", "sleeping": "9:30"}],
            "check": [
                "and",
                [">", "outdoor/temperature", 22],
                [">", "indoor/temperature",
                    ["switch", "system/presence", {"absent": 23, "awake": 25, "sleeping": 24}]
                ]
            ],
            "topic": {
                "shutter/east/set": 100,
                "shutter/west/set": 100
            }
        }
    }
}
```

This rule will move down the roller at day to protect against too much sun in summer:

- moves only down, it indoor and outdoor temperature are high
- request higher indoor temperature if you are present and not sleeping - closed rollers are not nice if you are there
- move the rollers down later if you are present, again to protect against dark rooms
- moves down only once - if you like to overrule manually it will not bug you

#### roller move up again at afternoon

If the roller moved down for sun protection, it needs to move up again

```JSON
{
    "rules": {
        "UpAfternoon": {
            "time": ["switch", "system/presence", {"absent": "17:00", "awake": "16:00", "sleeping": "17:00"}],
            "check": ["<=", "/time", ["-", "/sunset", 30]],
            "topic": {
                "shutter/east/set": 0,
                "shutter/west/set": 0
            }
        }
    }
}
```

- moves the roller up again, a little earlier, if you are at home
- but never, if sunset is near
- if you come home for example at 16:30, the roller will automatically move up on arrival

#### roller moves down at sunset

```JSON
{
    "rules": {
        "DownSunsetse": {
            "check": [">=", "/time", "/sunset"],
            "topic": { "shutter/east/set": "100" }
        },
        "DownSunsetsw": {
            "check": [">=", "/time", ["-", "/sunset", 20]],
            "topic": { "shutter/west/set": "100" }
        },
        "DownEvening": {
            "time": "23:00",
            "topic": {
                "shutter/east/set": 100,
                "shutter/west/set": 100
            }
        }
    }
}
```

The shutter are moving down at different times:

- Moving down on sunset
- Move one earlier than the other, if you have a terrace door. The first roller will warn you that the terrace door will be blocket in 20 minutes.
- It not down already, it fill (finally) move down at 23:00. If for example to stoped the roller to move down due to a long warm night, it will finally move down later.

If you have a motion detection in the terace you might also hinder the rollers to move down, if you are at the terace
