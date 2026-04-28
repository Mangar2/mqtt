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
const TopicMatch = require('@mangar2/topicmatch')
const CheckResult = require('./checkresult')

/**
 * Converts a timestamp to a string with the local time of day in brackets
 * @param {number} timestamp timestamp in milliseconds
 */
function timeInMillisecondsToLocalTimeString (timestamp) {
    return ' (' + new Date(timestamp).toLocaleTimeString() + ')'
}

/**
 * @private
 * @description Checks, if the events map contains all required topics
 * @param {Array} topics list of required topics
 * @param {Object} events map (topic: timestamp) of recent topics
 * @returns {CheckResult} check result
 */
function _isAllIncluded (topics, events) {
    const result = new CheckResult(false, '')
    if (types.isArray(topics)) {
        result.check = true
        for (const topic of topics) {
            result.andCheck(events[topic] !== undefined, topic + timeInMillisecondsToLocalTimeString(events[topic]))
            if (!result.check) {
                break
            }
        }
    } else if (types.isString(topics)) {
        result.setCheck(events[topics] !== undefined, topics + timeInMillisecondsToLocalTimeString(events[topics]))
    }
    return result
}

/**
 * @private
 * @description Checks, if the events map matches any topics
 * @param {Array} topics list of any topics
 * @param {Object} events map (topic: timestamp) of recent topics
 * @returns {CehckResult} check result
 */
function _isAnyIncluded (topics, events) {
    const result = new CheckResult(false, '')
    const topicMatch = new TopicMatch(topics)
    for (const event in events) {
        if (topicMatch.getFirstMatch(event) !== undefined) {
            result.setCheck(true, event + timeInMillisecondsToLocalTimeString(events[event]))
            break
        }
    }

    return result
}

/**
 * @private
 * @description Checks, if the events map is a subset of the allowed topics
 * @param {Array} allow topics of allowed topics
 * @param {Object} events map (topic: timestamp) of recent topics
 * @returns {boolean} check result
 */
function _isSubset (topics, events) {
    let result = true
    const topicMatch = new TopicMatch(topics)
    for (const event in events) {
        if (topicMatch.getFirstMatch(event) === undefined) {
            result = false
            break
        }
    }
    return result
}

/**
 * @private
 * @description Adds topics to the topic array
 * @param {string[]} topics array of topics
 * @param {string|string[]} add topics to acc
 * @returns {string[]} array of strings
 */
function _addTopics (topics, add) {
    if (types.isArray(add)) {
        topics.push(...add)
    } else if (types.isString(add)) {
        topics.push(add)
    }
    return topics
}

/**
 * @private
 * @description Checks if a list of events matches the rule
 * @param {Object} rule rule object to check events
 * @param {Array} rule.allOf list of required events
 * @param {Array} rule.anyOf list of events at least one is required
 * @param {Array} rule.noneOf list of not allowed events
 * @param {Array} rule.allow list of allowed events (if provided events not allowed and required are not allowed)
 * @param {Object} [motionEvents] map {topic: timestamp} of events classified as motions
 * @param {Object} [nonMotionEvents] map {topic: true} of events not classified as motions
 * @returns {boolean} true, if the list of events matches the rule or if 'events' is undefined
 */
module.exports = (rule, motionEvents, nonMotionEvents) => {
    let result = new CheckResult(true)
    const checkEvents = motionEvents !== undefined && (rule.allOf !== undefined || rule.anyOf !== undefined)
    const allEvents = { ...motionEvents, ...nonMotionEvents }
    if (checkEvents) {
        result = _isAllIncluded(rule.allOf, allEvents)
        if (!result.check) {
            result = _isAnyIncluded(rule.anyOf, allEvents)
        }
        if (result.check && _isAnyIncluded(rule.noneOf, allEvents).check) {
            result.setCheck(false)
        }
        if (result.check && types.isArray(rule.allow)) {
            const allow = [...rule.allow]
            _addTopics(allow, rule.allOf)
            _addTopics(allow, rule.anyOf)
            if (!_isSubset(allow, motionEvents)) {
                result.setCheck(false)
            }
        }
    }
    if (rule.doLog) {
        console.log('check events: %s', JSON.stringify(result))
        console.log(allEvents)
    }
    return result
}
