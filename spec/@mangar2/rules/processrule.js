/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

'use strict'

const types = require('@mangar2/types')
const Decision = require('@mangar2/decision')
const Message = require('@mangar2/message')
const { dateToTimeOfDayInSeconds } = require('@mangar2/time')
const RuleHistory = require('./rulehistory')
const checkEvents = require('./checkevents')
const Variables = require('./variables')

/**
 * @description Processes rules based on a set of variables
 * @param {Date} [date=now] current date
 * @param {number} [longitude = 51.476852] geographical longitued poistion of the automation target in degrees
 * @param {number} [latitude = -0.000500] geographical latitued poistion of the automation target in degrees
 */
class ProcessRule {
    constructor (date = new Date(), longitude = 51.476852, latitude = -0.000500) {
        this.date = date
        this._variables = new Variables(date, longitude, latitude)
        this._usedVariables = {}
        this._dateChanged = false
        this._history = new RuleHistory()
    }

    /**
     * @description Sets all variables
     * @param {Object} variables key/value object containing a list of variables
     */
    set variables (variables) {
        for (const name in variables) {
            const value = variables[name]
            this.setVariable(name, value)
        }
    }

    /**
     * @description Sets a single variable
     * @param {string} name name of the variable to set
     * @param {*} value value of the variable to set
     */
    setVariable (name, value) {
        this._variables.setValue(name, value)
    }

    /**
     * @description Gets the current date/time
     * @returns {Date} current date/time
     */
    get date () { return this._date }

    /**
     * @description Sets the current date/time
     * @param {Date} date current date
     */
    set date (date) {
        this._date = new Date(date.getTime())
        this._dateChanged = true
    }

    /**
     * @description Evaluates a value from a rule property
     * @param {Object|string|number} property rule property
     * @param {string|number} defaultValue value to return, if the property is not defined
     * @throws {Error} If the property includes a referenct to a variable that is not defined
     * @returns {EvalResult} property value, reason and variables
     * @private
     */
    _evalPropertyValue (property, defaultValue) {
        let result = { value: undefined, reason: '', variables: []}
        const term = property === undefined ? defaultValue : property
        if (term !== undefined) {
            const evalResult = this._eval(term)
            result = evalResult
        }
        return result
    }

    /**
     * Check, if the days of the week of the rule matches the current weekday
     * @param {Object} rule rule to process
     * @param {string|array} weekdays days in the week to check
     * @returns true, if weekdays is not specified or the day of the week matches the expectation
     */
    _checkWeekday(rule) {
        let found = 0

        if (rule.weekdays !== undefined) {
            let reqiredWeekdays = rule.weekdays
            const weekday = ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'][this.date.getDay()]
            if (types.isString(reqiredWeekdays)) {
                reqiredWeekdays = [reqiredWeekdays]
            }
            found = reqiredWeekdays.findIndex(e => e.toLowerCase() === weekday)

            if (rule.doLog) {
                console.log(`day of week evaluation: ${found}`)
            }
    
        }

        return found !== -1
    }

    /**
     * @description Checks, if the rule has a time constrains. The rule is valid if
     * it has a time constrains defining a time span and the current time is in the time span or it
     * has no time constrains
     * @param {Object} rule rule to check
     * @param {string} [rule.time] time of day to activate the time
     * @param {string} [rule.duration='6:00'] duration of the rule
     * @returns {boolean} true, if the rule is currently valid based on the time of day stored
     * @private
     */
    _checkTime (rule) {
        let result = rule.time === undefined
        const date = this._evalPropertyValue(rule.time).value
        const ONE_DAY_IN_SECONDS = 24 * 60 * 60
        if (types.isDate(date)) {
            const duration = this._evalPropertyValue(rule.duration, '6:00').value
            const timeInSeconds = dateToTimeOfDayInSeconds(date)
            const durationInSeconds = dateToTimeOfDayInSeconds(duration)
            const timeOfDayInSeconds = dateToTimeOfDayInSeconds(this.date)
            const laterSameDay = timeOfDayInSeconds >= timeInSeconds && timeOfDayInSeconds < durationInSeconds + timeInSeconds
            const earlierNextDay = timeOfDayInSeconds < durationInSeconds + timeInSeconds - ONE_DAY_IN_SECONDS
            result = laterSameDay || earlierNextDay
        }

        result = result && this._checkWeekday(rule)

        if (rule.doLog) {
            console.log('time evaluation: %s', result)
        }

        return result
    }

    /**
     * @description Evaluates a JSON formatted program
     * @param {Array|boolean|string|number} term term to evaluate
     * @returns {EvalResult} evaluation result with value, reason and variables
     * @private
     */
    _eval (term) {
        const decisionMaker = new Decision(this._variables.getAllVariables())
        const result = decisionMaker.eval(term)
        this._usedVariables = { ...this._usedVariables, ...result.variables }
        return result
    }

    /**
     * @private
     * @description Evaluates the rule 'check' constrains.
     * @param {Object} rule rule to check
     * @param {string} [rule.check] check function in JSON format
     * @throws {Error} on error in the decision rule
     * @returns {boolean} true, if rule has no 'check' property or the 'check' property evaluates to true
     */
    _checkDecision (rule) {
        const value = rule.check === undefined
        const reason = ''
        let result = { value, reason }
        if (rule.check !== undefined) {
            result = this._eval(rule.check)
        }
        if (rule.doLog) {
            console.log('check evaluation: %s', JSON.stringify(result))
        }
        return result
    }

    /**
     * @private
     * @description Adds a list of topics to the used variables
     * @param {Array} topics list of topics to add
     */
    _addToUsedVariables (topics) {
        if (types.isArray(topics)) {
            for (const topic of topics) {
                const value = this._variables.getValue(topic)
                this._usedVariables[topic] = value
            }
        }
    }
    
    /**
     * @description Gets a list of needed (external) variables for the rule
     * @param {Object} rule rule to check
     * @returns {Object} {name: value} list of needed variables including current value
     */
    determineNeededVariables (rule) {
        this._usedVariables = {}
            
        this._checkTime(rule)
        this._checkDecision(rule)
        this._evalPropertyValue(rule.value)
        this._addToUsedVariables(rule.allOf)
        this._addToUsedVariables(rule.anyOf)
        this._addToUsedVariables(rule.allow)
        this._addToUsedVariables(rule.noneOf)
    
        let result = {}
        for (const topic in this._usedVariables) {
            if (this._variables.isExternVariable(topic)) {
                const value = this._variables.getValue(topic)
                result[topic] = value
            }
        }
            
        return result
    }

    /**
     * Creates a message
     * @param {string} topic topic of the message
     * @param {string|number|objct} valueProperty rule value property
     * @param {string} reason reason so far 
     * @param {0, 1, 2} qos Quality of service
     * @returns {Message} message
     */
    _createMessage(topic, valueProperty, reason, qos) {
        const evalResult = this._evalPropertyValue(valueProperty)
        let newReason = reason
        if (evalResult.reason) {
            newReason += ', value: ' + evalResult.reason
        }
        const result = new Message(topic, evalResult.value, newReason, this._date)
        result.qos = qos
        return result
    }

    /**
     * @private
     * @description Creates messages based on a rule
     * @param {Object} rule rule to check
     * @param {Object} decisionResult evaluation result of the rule "check" property
     * @param {string} decisionResult.reason expains the rule evaluation
     * @returns {{messages: Message[], usedVariables}} Array of messages and object of variables used.
     */
    _getMessagsFromRule (rule, decisionResult) {
        const result = { usedVariables: {}, messages: [] }
        const { topic, name } = rule
        let reason = 'Rule: ' + name
        if (decisionResult.reason !== '') {
            reason += ', ' + decisionResult.reason
        }
        const qos = rule.qos === undefined ? 1 : rule.qos
        if (types.isArray(topic)) {
            for (const oneTopic of topic) {
                const message = this._createMessage(oneTopic, rule.value, reason, qos)
                result.messages.push(message)
            }
        } else if (types.isObject(topic)) {
            for (const oneTopic in topic) {
                const message = this._createMessage(oneTopic, topic[oneTopic], reason, qos)
                result.messages.push(message)
            }
        } else if (types.isString(topic)) {
            const message = this._createMessage(topic, rule.value, reason, qos)
            result.messages.push(message)
        }
        return result
    }

    /**
     * @description Derives all messages from the rule
     * @param {Object} rule rule to check
     * @param {Object} [motionEvents] map {topic: timestamp} list of events classified as motions
     * @param {Object} [nonMotionEvents] {topic: true} list of events not classified as motions
     * @throws {Error} on error in the decision rule
     * @returns {{messages: Message[], usedVariables}} Array of messages and object of variables used.
     */
    _deriveAllMessagesFromRule(rule, motionEvents, nonMotionEvents) {
        let result = { usedVariables: {}, messages: [] }
        const isInactiveActive = (rule.active === false)
        if (rule.doLog && isInactiveActive) {
            console.log('Rule is inactive (active: false)')
        }
        if (!isInactiveActive && this._checkTime(rule)) {
            const decisionResult = this._checkDecision(rule)
            if (decisionResult.value) {
                const checkResult = checkEvents(rule, motionEvents, nonMotionEvents)
                if (checkResult.check) {
                    if (checkResult.reason !== '') {
                        const spacer = decisionResult.reason === '' ? '' : '; '
                        decisionResult.reason = 'Events: ' + checkResult.reason + spacer + decisionResult.reason
                    }
                    result = this._getMessagsFromRule(rule, decisionResult)
                }
            }
        }
        result.usedVariables = this._usedVariables
        return result
    }

    /**
     * @description Processes a rule and if the rule demands, create a message
     * @param {Object} rule rule to check
     * @param {Object} [motionEvents] map {topic: timestamp} list of events classified as motions
     * @param {Object} [nonMotionEvents] {topic: true} list of events not classified as motions
     * @throws {Error} on error in the decision rule
     * @returns {{messages: Message[], usedVariables}} Array of messages and object of variables used.
     */
    check (rule, motionEvents, nonMotionEvents) {
        if (rule.doLog) {
            console.log('Logging rule: %s', rule.name)
        }    
        if (this._dateChanged) {
            this._variables.calculateInternalVariables(this.date)
        }
        const result = this._deriveAllMessagesFromRule(rule, motionEvents, nonMotionEvents)
        if (rule.doLog) {
            console.log('Derived Messages:')
            console.log(JSON.stringify(result, null, 2))
        }
        const allDerivedMessages = result.messages

        result.messages = this._history.extractMessagesToSend(rule, this.date, result.messages)
        if (rule.doLog) {
            console.log('Extracted Messages:')
            console.log(JSON.stringify(result, null, 2))
        }
        this._history.update(rule, this.date, allDerivedMessages, result.messages)

        return result
    }
}

module.exports = ProcessRule
