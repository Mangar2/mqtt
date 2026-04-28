/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * 
 * @description
 * The rule history stores the history for each rule and each topic in the rule
 * For all [rulename][topic] entries, it stores the 
 * - value: The current value of the message created by the rule 
 * - firstFound: The date the same value was first found
 * - lastSent: The date the message was last sent
 */

'use strict'

const types = require('@mangar2/types')

/**
 * @typedef {Object} HistoryEntry
 * @property {Date} firstFound time the message was found/generated first
 * @property {Date} lastSent time the massage was sent last
 * @property {string|number} value value of the message
 */

/**
 * @typedef {Object} Decision
 * @property {boolean} value boolean value of the decision
 * @property {string} reason explanation of the decision
 */

/**
 * @private
 * @description
 * Creates a class storing a history of topic/value pairs per rule to detect redundant messages
 */
class RuleHistory {
    constructor () {
        this._history = {}
    }

    /**
     * Gets the history entry of a rule to a topic
     * @param {string} name rule name
     * @param {string} topic message topic
     * @returns {HistoryEntry} value of the last message produced by this rule
     */
    _getHistoryEntry (name, topic) {
        let result
        if (this._history[name] !== undefined && this._history[name][topic] !== undefined) {
            result = this._history[name][topic]
        }
        return result
    }

    /**
     * @private
     * @description
     * Gets the date of the latest history entry for a rule
     * @param {string} name rule name
     * @returns {Date|undefined} latest date for the rules history entry
     */
    __getLatestHistoryDate (name) {
        const ruleHistory = this._history[name]
        let latestRuleHistoryDate
        if (ruleHistory !== undefined) {
            for (const topic in ruleHistory) {
                const entry = ruleHistory[topic]
                if (latestRuleHistoryDate === undefined || latestRuleHistoryDate < entry.date) {
                    latestRuleHistoryDate = entry.date
                }
            }
        }
        return latestRuleHistoryDate
    }

    /**
     * @private
     * @description
     * Checks, if two values are equal by === for all values but dates. getTime() is used to compare dates
     * @param {any} value1 
     * @param {any} value2 
     * @returns {boolean} true, if both values are equal
     */
    _isMessageValueEqual(value1, value2) {
        if (types.isDate(value1) && types.isDate(value2)) {
            return value1.getTime() === value2.getTime()
        } else {
            return value1 === value2
        }
    }

    
    /**
     * @private
     * @description
     * Checks if a message is in cooldown time to prevent sending messages too often. 
     * @param {Object} rule rule to check
     * @param {string} rule.name rule name
     * @param {number} rule.cooldownInSeconds time between two updates with the same value.
     * The message with only be sent if it has a different value. There is never
     * a cooldownd, if this cooldownInSeconds is undefined
     * @param {Date} date current date/time
     * @param {Message} message created by the rule
     * @returns {Decision} decision (true/false) with explanation
     */
    _isInCooldown(rule, date, message) {
        const MILLISECONDS_IN_A_SECOND = 1000
        const result = {
            value: false,
            hasCooldownValue: types.isNumber(rule.cooldownInSeconds),
            reason: 'Not in cooldown, because rule has no "rule.cooldownInSeconds" parameter'
        }
        const historyEntry = this._getHistoryEntry(rule.name, message.topic)
        if (result.hasCooldownValue) {
            const cooldown = rule.cooldownInSeconds * MILLISECONDS_IN_A_SECOND
            const lastSentPlusCooldown = historyEntry.lastSent.getTime() + cooldown
            result.value = lastSentPlusCooldown > date.getTime()
            if (result.value === false) {
                result.reason = 'Not in cooldown because lastSent plus cooldown (' + lastSentPlusCooldown + ') <= time '
            } else {
                result.reason = 'In cooldown because lastSent plus cooldown (' + lastSentPlusCooldown + ') > time '
            }
        }
        return result
    }

    /**
     * @private
     * @description
     * Checks if a message is redundant. It is redundant, if it produces the same value as before
     * and if a possible timeout (cooldownInSeconds) has not been passed
     * @param {Object} rule rule to check
     * @param {string} rule.name rule name
     * @param {number} rule.cooldownInSeconds time between two updates with the same value.
     * The message with only be sent if it has a different value. There is never
     * a cooldownd, if this cooldownInSeconds is undefined
     * @param {Date} date current date/time
     * @param {Message} message created by the rule
     * @returns {boolean} true, if the message is redundant
     */
    _isMessageRedundant (rule, date, message) {
        const historyEntry = this._getHistoryEntry(rule.name, message.topic)
        
        let isRedundant = false
        
        if (historyEntry !== undefined && historyEntry.lastSent !== undefined) {
            const messageHasSameValue = this._isMessageValueEqual(historyEntry.value, message.value)
            const isInCooldown = this._isInCooldown(rule, date, message)

            if (isInCooldown.value) {
                // Messages calculating a date value are not treated as different in a cooldown phase
                // Dates contains milliseconds thus they always change fast, cooldown shall prevent 
                // too many date changes
                const messageIsDate = types.isDate(message.value)
                isRedundant = messageHasSameValue || messageIsDate
            } else if (!isInCooldown.hasCooldownValue) {
                // Messages triggered by events are never treated redundant even if they send the same value
                // Ten detected movements shall result in 10 actions, no matter how fast they are
                // We only want to prevent calculated actions, if the action is aways the same like 
                // "Switch off light after 9:00" - we do not want to switch it off again and again
                const isTriggeredByEvent = types.isArray(rule.anyOf) || types.isArray(rule.allOf) 
                isRedundant = messageHasSameValue && !isTriggeredByEvent
            } 
        }
        return isRedundant
    }

    /**
     * @private
     * @description
     * Checks if a message is too early. It is too early, if the delayInSeconds time has not passed
     * since the message first triggered
     * @param {Object} rule rule to check
     * @param {string} rule.name rule name
     * @param {number} rule.delayInSeconds time the rule needs to constantly produce the same message
     * before it will be sent. 
     * @param {Date} date current date/time
     * @param {Message} message created by the rule
     * @returns {boolean} true, if the message is too early
     */
    _isMessageTooEarly (rule, date, message) {
        let tooEarly = false
        if (types.isNumber(rule.delayInSeconds)) {
            tooEarly = true
            const MILLISECONDS_IN_A_SECOND = 1000
            const historyEntry = this._getHistoryEntry(rule.name, message.topic)
            const messageFound = historyEntry !== undefined && historyEntry.value === message.value
            if (messageFound) {
                const delayInMilliseconds = rule.delayInSeconds * MILLISECONDS_IN_A_SECOND
                tooEarly = historyEntry.firstFound.getTime() + delayInMilliseconds >= date.getTime()
            } 
        }
        return tooEarly
    }
    
    /**
     * @private
     * @description
     * Updates the history entry for a rule
     * @param {string} name name of the rule
     * @param {Date} curDate current date/time
     * @param {string} topic topic to update
     * @param {any} value value to update
     */
    _updateHistory (name, curDate, topic, value) {
        const firstFound = new Date(curDate.getTime())
        const lastSent = new Date(curDate.getTime())
        if (this._history[name] === undefined) {
            this._history[name] = {}
        }
        const ruleHistory = this._history[name]
        const newValue = ruleHistory[topic] === undefined || value !== ruleHistory[topic].value
        if (newValue) {
            ruleHistory[topic] = { firstFound, lastSent, value }
        } else {
            ruleHistory[topic].lastSent = lastSent
        }
        
    }

    /**
     * @private
     * @description Gets an array of topics of the rule
     * @param {Object} rule rule to get topic from
     * @param {Object|Array|string} rule.topic topic(s) of the rule
     * @returns {Array} list of rule topics
     */
    _getTopicsFromRule (rule) {
        const topics = rule.topic
        const result = []
        if (types.isString(topics)) {
            result.push(topics)
        } else if (types.isArray(topics)) {
            result.push(...topics)
        } else if (types.isObject(topics)) {
            const objectTopics = Object.keys(topics)
            result.push(...objectTopics)
        }
        return result
    }

    /**
     * @description
     * Checks the messages produced by a rule and removes messages that are either redundant
     * or too early
     * @param {Object} rule rule currently processing
     * @param {string} rule.name name of the rule
     * @param {Date} date date of the rule
     * @param {Array} messages list of messages providing new information
     */
    extractMessagesToSend (rule, date, messages) {
        const result = []
        for (const message of messages) {
            const isRedundant = this._isMessageRedundant(rule, date, message)
            const isTooEarly = this._isMessageTooEarly(rule, date, message)
            if (rule.doLog) {
                console.log('Redundant: %s, tooEarly: %s', isRedundant, isTooEarly)
                console.log(message)
            }
            if (!isRedundant && !isTooEarly) {
                result.push(message)
            }
        }
        return result
    }

    /**
     * @description
     * Updates the history information by all messages created by a rule, no matter if it 
     * will be sent
     * This replaces a send information in the history on changed value but not for values
     * of type date - they are an exception. This prevents rules sending the current date/time
     * to send it again each millisecond
     * @param {Object} rule rule currently processing
     * @param {string} rule.name name of the rule
     * @param {Date} curDate current date/time
     * @param {Array} messages full list of messages 
     */
    _updateHistoryForAllMessages (rule, curDate, messages) {
        const { name } = rule
        const firstFound = new Date(curDate.getTime())
        let lastSent = undefined

        for (const message of messages) {
            const { topic, value } = message
            if (this._history[name] === undefined) {
                this._history[name] = {}
            }
            const ruleHistory = this._history[name]
            const newValue = ruleHistory[topic] === undefined || value !== ruleHistory[topic].value
            const messageIsDate = types.isDate(value)
            if (messageIsDate && ruleHistory[topic] !== undefined) {
                lastSent = ruleHistory[topic].lastSent
            }
            if (newValue) {
                ruleHistory[topic] = { firstFound, lastSent, value }
            } 
        }
    }

    /**
     * @description
     * Updates the history information by all messages that are not redundant. We skip redundant
     * messages to keep the information when the message has been sent first
     * @param {Object} rule rule currently processing
     * @param {string} rule.name name of the rule
     * @param {Date} curDate current date/time
     * @param {Array} messages list of messages to send
     */
    _updateHistoryForMessagesToSend (rule, curDate, messages) {
        const { name } = rule
        const lastSent = new Date(curDate.getTime())
    
        for (const message of messages) {
            const { topic } = message
            const ruleHistory = this._history[name]
            ruleHistory[topic].lastSent = lastSent
        }
    }

    /**
     * @description
     * Clear all message entries, if the rule did not produce any message and the rule is 
     * not waiting for a cooldown (in the cooldown time, the rule will not produce messages, but
     * must remember the date/time of the last message sent)
     * @param {Object} rule rule currently processing
     * @param {string} rule.name name of the rule
     * @param {Date} date date of the rule
     * @param {Array} messages list of messages providing new information
     */
    _clearHistoryIfNoMessagesAvailable(rule, date, messages) {
        if (rule.cooldownInSeconds === undefined && messages.length === 0) {
            const topics = this._getTopicsFromRule(rule)
            for (const topic of topics) {
                this._updateHistory(rule.name, date, topic, null)
            }
        }
    }

    /**
     * @description
     * Updates the history information for the rule
     * @param {Object} rule rule currently processing
     * @param {string} rule.name name of the rule
     * @param {Date} date date of the rule
     * @param {Array} allMessages list of all detected messages
     * @param {Array} messagesToSend list of messages that will be send
     */
    update (rule, date, allMessages, messagesToSend) {
        this._updateHistoryForAllMessages(rule, date, allMessages)
        this._updateHistoryForMessagesToSend(rule, date, messagesToSend)
        this._clearHistoryIfNoMessagesAvailable(rule, date, allMessages)
    }

    /**
     * Gets the events not already used for an older decision from the list of events
     * @param {Object} rule rule to get events from
     * @param {string} rule.name name of the rule
     * @param {Object} events Map with "topic:date" entries
     */
    getNewEvents (rule, events) {
        const result = {}
        const latestRuleHistoryDate = this.__getLatestHistoryDate(rule.name)
        for (const event in events) {
            const eventDate = events[event]
            if (latestRuleHistoryDate === undefined || eventDate > latestRuleHistoryDate) {
                result[event] = eventDate
            }
        }
        return result
    }
}

module.exports = RuleHistory
