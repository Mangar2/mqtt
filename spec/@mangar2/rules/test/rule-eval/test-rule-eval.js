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

const VERBOSE = false
const { Rules, ProcessRule } = require('@mangar2/rules')

const { timeOfDayStringToDate } = require('@mangar2/time')

const Testrun = require('@mangar2/testrun')
const testrun = new Testrun(VERBOSE)

/**
 * Gets an event list in the right format {topic:date, ...}
 * @param {Array} events array of events
 * @param {Date} date test date/time
 * @returns {Object} event list in right format
 */
function formatEvents (events, date) {
    const result = {}
    if (events !== undefined) {
        for (const event of events) {
            result[event] = date
        }
    }
    return result
}

/**
 * Creates a nearby date with a time of the day or weekday set by the test specifications
 * @param {any} test test parameters
 * @param {string} [test.time] string representing a time of the day
 * @param {string} [test.weekday] string representing the weekday (abbreviated to 3 letters)
 * @returns a date
 */
function createDate(test) {
    const time =  test.time !== undefined ? timeOfDayStringToDate(test.time) : new Date()
    const reqWeekday = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'].findIndex(e => e === test.weekday)
    const curWeekday = time.getDay()
    const weekdayDifference = reqWeekday === -1 ? 0 : reqWeekday - curWeekday
    time.setDate(time.getDate() + weekdayDifference)
    return time
}

testrun.on('prepare', testcase => {
    const processRule = new ProcessRule(new Date())
    const rulesObject = new Rules()
    let { rule, rules } = testcase
    rules = rules ? rules : { 'rule': rule }
    for (const name in rules) {
        rulesObject.setRule(rules[name])
    }
    return { processRule, rulesObject }
})

function runTest (test, testObjects) {
    const { processRule, rulesObject } = testObjects
    const checkTime = createDate(test)
    // const testRules = new Rules(rules, checkRule)
    const motionEvents = formatEvents(test.motionEvents, checkTime)
    const nonMotionEvents = formatEvents(test.nonMotionEvents, true)
    processRule.variables = test.variables
    processRule.date = checkTime
    const result = { messages: [], usedVariables: {}, rules: []}
    for (const rule of rulesObject.getRules()) {
        processRule.determineNeededVariables(rule)
        const check = processRule.check(rule, motionEvents, nonMotionEvents)
        result.rules.push(rule)
        result.messages = [...result.messages, ...check.messages]
        result.usedVariables = {...result.usedVariables, ...check.usedVariables}
    }
    testrun.unitTest.replaceRec(test.expected, { '<time>': checkTime })
    return result
}

testrun.on('run', (test, testObjects) => {
    return runTest(test, testObjects)
})

testrun.on('break', (test, testObjects) => {
    runTest(test, testObjects)
})


testrun.run(
    [
        'isactive',
        'activation',
        'cooldown',
        'delay',
        'error',
        'time',
        'value',
        'variables',
        'require',
        'allow',
        'deny',
        'timecalc',
        'weekdays'
    ],
    __dirname, 104)

