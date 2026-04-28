/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * Automation bases on a list of rules. Rules are processed every minute (configurabel) or after each
 * new message/information arriving
 * For each rules
 * 1. It is checked if the rule is currently active (in package "rules/processrule/_checkTime") using 
 *    "time" and "duration" properties (default always active)
 * 2. If active, it is checked if the rules formular evaluates to true using the "check" property
 *    in package "decision/decision" (default always true)
 * 3. If check is true, it checks the event properties in package "rules/checkevents" (default always true)
 *    Two types of events are supported: 
 *    3.1 "Motion events" are collected and checked if allOf/anyOf/noneOf ... happend recently
 *    3.2 "NonMotion events" are used as one-time trigger to start a rule (example "on button pressed")
 * 4. If the rule passed all these tests a set of messages described by the rule are created using 
 *    properties topic and value (many)
 * 5. In this step, all redundant messages are removed to prevent the system to send the same message
 *    more than once. A message history stores the last message for each topic, if the current message
 *    is identical to the last message, the new redundant message is removed. 
 *    (in rules/ruleshistory/extractNewMessages)
 *    One exception: if the rule specifies cooldownInSeconds, a message is resent after the cooldown
 * 6. The history is then updated in the last step. All messages are removed, if the rule would not send
 *    the same message again - if cooldownInSeconds is not specified. With cooldownInSeconds, a rule message  
 *    is resent after cooldownInSeconds independently from a change of a value
 *    Example: a rule sends a message when the power consumption is above p. As soon as
 *    it drops below p, the message is removed from the history. If it now rises above p again, it is sent again.
 */
'use strict'
const DEBUG = false

const EventHistory = require('./eventhistory')
const sanitize = require('./configuration.js')
const TopicMatch = require('@mangar2/topicmatch')
const { Rules, ProcessRule } = require('@mangar2/rules')
const types = require('@mangar2/types')
const { timeOfDayStringToDate } = require('@mangar2/time')
const Message = require('@mangar2/message')

const { saveRulesToFileStore } = require('@mangar2/automation/readrules')

const errorLog = (err) => { require('@mangar2/errorlog')(err, DEBUG) }

/**
 * Creates an automation class. Automation is a automation rule processor
 * This class is designed to work togehter with the runservices service to provide event based
 * automations.
 * @param {Object} configuration
 * @param {Array} [configuration.motionTopics] list of topics to subscribe to see all motions
 * @param {string} [configuration.presenceVariable='$SYS/presence'] name of the variable to store the presence flag
 * @param {number} [configuration.subscribeQoS=1] quality of service for message subscription
 * @param {number} configuration.longitued longitude of the place to automate
 * @param {number} configuration.latitude latitude of the place to automate
 */
class Automation {
    constructor (configuration) {
        const sanitizedOptions = sanitize(configuration)
        this._presenceTopic = sanitizedOptions.presenceTopic
        this._subscribeQoS = sanitizedOptions.subscribeQoS
        this._motionTopics = sanitizedOptions.motionTopics
        this._filestoreOptions = sanitizedOptions.filestore
        this._history = new EventHistory()
        this._processRule = new ProcessRule(new Date(), configuration.longitude, configuration.latitude)
        this._setVariable(this._presenceTopic, 'initial')
        this._simulation = false
        this._usedVariables = {}
    }

    /**
     * Gets the sanitized options for the filestore
     */
    get filestoreOptions() {
        return this._filestoreOptions
    }
    
    /**
     * Sets/replaces the rules tree
     * @param {Object} rulesTree tree with 'rules' elements
     */
    setRules (rulesTree) {
        this._rules = new Rules(rulesTree)
    }

    /**
     * Sets a rule from a set rule message
     * @param {string} value value of a set rule message
     * @param {string} ruleName name of the rule
     * @returns {Message[]} result messages
     */
    _setRuleFromValue(value, ruleName) {
        let resultMessages = []
        if (types.isString(value)) {
            const rule = JSON.parse(value)
            if (rule.name === undefined) {
                rule.name = ruleName
            }
            const isValid = this._rules.setRule(rule)
            resultMessages.push(new Message('$SYS/automation/rules/' + ruleName, JSON.stringify(rule), 'update message'))
            if (!isValid) {
                resultMessages.push(new Message('$SYS/automation/rules/' + ruleName, 'invalid rule', 'update message'))
            }
        }
        return resultMessages
    }

    /**
     * Adds or deletes a rule based on a rule update message
     * @param {Message} message 
     */
    _handleRuleMessage(message) {
        const { topic, value } = message
        if (!topic.endsWith('/set') || !topic.startsWith('$SYS/automation/rules/')) {
            return []
        }
        const resultMessages = []
        const remainder = topic.replace('$SYS/automation/rules/', '')
        let nameChunks = remainder.split('/')
        nameChunks.pop()
        const ruleName = nameChunks.join('/')
        if (value === 'delete') {
            this._rules.deleteRule(ruleName)
            resultMessages.push(new Message('$SYS/automation/rules/' + ruleName, 'deleted', 'delete message'))
        } else {
            resultMessages.push(...this._setRuleFromValue(value, ruleName))
        }
        saveRulesToFileStore(this._rules.getRuleTree(), this.filestoreOptions)
        for (const message of resultMessages) {
            message.qos = 1
        }
        return resultMessages
    }

    /**
     * Sets a single variable (for example from a message)
     * @param {string} name name of the variable
     * @param {string|number} value value of the variable
     * @private
     */
    _setVariable (name, value) { this._processRule.setVariable(name, value) }

    /**
     * Clears the automation history
     * @private
     */
    _clearEventHistory () {
        this._history.clear()
    }

    /**
     * Handles a message in simulation mode
     * @param {Message} message message received in simulation mode
     * @private
     */
    _handleSimulation (message) {
        const isSimulationTopic = message.topic.indexOf('$SYS/simulation/') !== -1
        if (isSimulationTopic) {
            if (message.topic === '$SYS/simulation/date') {
                const simulationDate = timeOfDayStringToDate(message.value)
                console.log('%s simulation date changed', simulationDate.toLocaleString())
                const result = this.processTasks(simulationDate, true)
                for (const logMessage of result.messages) {
                    console.log('%s %s=%s [%s]',
                        simulationDate.toLocaleString(), logMessage.topic, logMessage.value, logMessage.reason[0].message)
                }
            } else if (message.topic === '$SYS/simulation/end') {
                this._simulation = false
                console.log('%s simulation stopped', (new Date()).toLocaleString())
            } else {
                message.topic = message.topic.replace('$SYS/simulation/', '')
                const date = timeOfDayStringToDate(message.reason[0].timestamp)
                message.reason[0].timestamp = date.toISOString()
                console.log('%s %s=%s', message.getDateOfNewestChange().toLocaleString(), message.topic, message.value)

                const topicMatch = new TopicMatch(this._motionTopics)
                if (topicMatch.getFirstMatch(message.topic) !== undefined) {
                    this._history.addEvent(message, true)
                }
                this._setVariable(message.topic, message.value)
            }
        }
    }

    /**
     * Checks receives messages and adds them to motion and variable lists
     * @param {Message} message received message
     * @returns {Message[]} list of messages as result (always empty)
     * @private
     */
    _processMessage (message) {
        const { topic, value } = message
        let resultMessages = []
        if (topic === '$SYS/simulation/date' && !this._simulation) {
            this._simulation = true
            console.log('simulation started')
        }
        if (this._simulation) {
            this._handleSimulation(message)
        } else if (topic.startsWith('$SYS/automation/rules')) {
            resultMessages = this._handleRuleMessage(message)
        } else {
            const topicMatch = new TopicMatch(this._motionTopics)
            const isMotion = topicMatch.getFirstMatch(topic) !== undefined
            this._history.addEvent(message, isMotion)
            this._setVariable(topic, value)
        }
        return resultMessages
    }

    /**
     * Checks all rules and returns needed variables
     * @returns {{variables: { [index:string]: string|number}, messages: Message[]}} 
     * map of used variables (variable name/variable value) and error messages
     * @private
     */
    _checkRules() {
        return this._rules.checkRules(this._processRule)
    }

    /**
     * Get all the variables to subscribe to
     * @returns {string[]} array of subscriptions
     */
    getSubscriptions () {

        const { variables } = this._checkRules()
        const subscriptions = {}
        const qos = this._subscribeQoS
        for (const topic of this._motionTopics) {
            subscriptions[topic] = qos
        }
        const topicMatch = new TopicMatch(this._motionTopics)
        for (const variable in variables) {
            const isNew = topicMatch.getFirstMatch(variable) === undefined
            if (isNew) {
                subscriptions[variable] = qos
            }
        }
        subscriptions['$SYS/simulation/#'] = 1
        subscriptions['$SYS/automation/#'] = 1
        return subscriptions
    }

    /**
     * Check, if the latest movement list is not old - the last time is not longer ago than a provided timeout
     * @param {Date} curDate current date
     * @param {number} latestMotionTimestamp timestamp of the last motion in milliseconds
     * @param {number} [timoutInMilliseconds=ONE_MINUTE]
     * @returns {boolean} true, if the motions are not outdated
     * @private
     */
    _isMotionListActual (curDate, latestMotionTimestamp, timoutInMilliseconds = 60 * 1000) {
        const noMoveTimespan = curDate.getTime() - latestMotionTimestamp
        return noMoveTimespan < timoutInMilliseconds
    }

    /**
     * Check that the latest motion timestamp is "durationWithoutMovementInMinutes" time ago. Returns true,
     * if durationWithoutMOvementInMinutes is undefined
     * @param {Date} curDate current date
     * @param {numer} latestMotionTimestamp timestamp of the last move in milliseconds
     * @param {number} [durationWithoutMovementInMinutes] required amount of minutes passed since last move
     * @returns {boolean} true, if the last move is long enough ago
     * @private
     */
    _isPassedEnoughTimeWithoutMotion (curDate, latestMotionTimestamp, durationWithoutMovementInMinutes) {
        let result = true
        if (types.isNumber(durationWithoutMovementInMinutes)) {
            const noMoveTimespan = curDate.getTime() - latestMotionTimestamp
            const ONE_MINUTE = 60 * 1000
            result = noMoveTimespan >= durationWithoutMovementInMinutes * ONE_MINUTE
        }
        return result
    }

    /**
     * Sets the internal variable values from messages to send
     * @param {Message[]} messages array of messages to send
     * @private
     */
    _setVariablesFromOwnMessages (messages) {
        for (const message of messages) {
            this._setVariable(message.topic, message.value)
        }
    }

    /**
     * Prints/shows the variable used
     * @param {Object} usedVariables name: value map of varialbes
     * @private
     */
    _showVariables (usedVariables) {
        for (const variable in usedVariables) {
            const value = usedVariables[variable]
            if (value === undefined && !(variable in this._usedVariables)) {
                console.log('variable not defined: %s', variable)
                this._usedVariables[variable] = value
            }
        }
    }

    /**
     * @private
     * @documentation Processes a single rule and returns the result
     * @param {Date} date current date
     * @param {Object} rule current rule processed
     * @param {Object} recentEvents information about the recent motions
     * @param {number} recentEvents.timestamp timestamp of the last motion detected
     * @param {Object} recentEvents.motions list of motion events
     * @param {Object} recentEvents.nonMotions list of non motion events. A non motion event is a message created before in the same 
     * process rules loop. It is fully deleted at the end of a process rules loop
     * @returns {Object} result of rule processing with messages and used variables
     * @memberof Automation
     */
    _processSingleRule (date, rule, recentEvents) {
        let result = { messages: [], usedVariables: {} }
        if (this._isPassedEnoughTimeWithoutMotion(date, recentEvents.timestamp, rule.durationWithoutMovementInMinutes)) {
            let activeMotion = recentEvents.motions
            if (rule.durationWithoutMovementInMinutes === undefined && !this._isMotionListActual(date, recentEvents.timestamp)) {
                activeMotion = {}
            }
            result = this._processRule.check(rule, activeMotion, recentEvents.nonMotions)
        }
        return result
    }

    /**
     * @private
     * @description Adds the result of the last rule to the total result
     * @param {Object} totalResult result of processed rules (will be modified)
     * @param {Object} lastResult result of last processed rule
     * @returns {Object} combined result
     * @memberof Automation
     */
    _combineCheckResults (totalResult, lastResult) {
        const newMessages = lastResult.messages
        if (types.isArray(newMessages) && (newMessages.length > 0) && types.isObject(newMessages[0])) {
            totalResult.messages = [...totalResult.messages, ...newMessages]
        }
        totalResult.usedVariables = { ...totalResult.usedVariables, ...lastResult.usedVariables }
        return totalResult
    }

    /**
     * Loops through all rules and processes them
     * @param {Date} [date] current date/time
     * @returns {Message[]} list of messages to process
     */
    processTasks (date = new Date(), simulation = false) {
        if (simulation !== this._simulation) return { messages: [] }
        let result = { messages: [], usedVariables: {} }
        this._processRule.date = date
        const recentEvents = this._history.getLatestEvents()
        for (const rule of this._rules.getRules()) {
            try {
                const checkResult = this._processSingleRule(date, rule, recentEvents)
                result = this._combineCheckResults(result, checkResult)
            } catch (err) {
                this._rules._invalidateRule(rule.name)
                err.message = 'Error in rule "' + rule.name + '": ' + err.message
                errorLog(err)
            }
        }
        if (result.messages.length > 0) {
            this._setVariablesFromOwnMessages(result.messages)
        }
        this._showVariables(result.usedVariables)
        this._history.clearNonMotionEvents()
        return result
    }

    /**
     * Fully handles an mqtt message
     * @param {Message} mqttMessage the received mqtt message
     * @returns {Message[]} array of reply messages
     */
    handleMessage (mqttMessage) {
        let messages = []
        try {
            const responseMessages = this._processMessage(mqttMessage)
            const ruleMessages = this.processTasks().messages
            messages = [...ruleMessages, ...responseMessages]
        } catch (err) {
            errorLog(err)
        }
        return messages
    }
}

module.exports = Automation
