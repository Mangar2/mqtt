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
const Automation = require('./automation')

/**
 * @private
 * Converts an amount of minutes passed in a day to a time of day string
 * @param {number} minutes amount of minutes passed in the current day
 * @returns {string} time of day in HH:MM format
 */
function minutesToTimeOfDay (minutes) {
    const minutesInHour = minutes % 60
    const hours = (minutes - minutesInHour) / 60
    let result = hours + ':'
    if (minutesInHour < 10) {
        result += '0'
    }
    result += minutesInHour
    return result
}

/**
 * Tests an array of rules with a szenario (presence changing at certain times) and variables
 * @param {Object[]} rules rules to test
 * @param {Object} szenario all szenario data, including maps from time of day strings formatted in 'HH:MM' to presence values
 * Required are: 'longituede, latitude, date'
 * @param {Object} [variables={}] defined variables
 * @returns {Message[]} list of messages to process
 */
function testRules (rules, szenario, variables = {}) {
    if (!types.isString(variables['$SYS/presence'])) {
        variables['$SYS/presence'] = 'sleeping'
    }
    if (!types.isObject(szenario)) {
        throw Error('szenario is missing')
    }
    let messages = []
    const automation = new Automation(szenario, rules)
    for (let timeInMinutes = 0; timeInMinutes <= 24 * 60; timeInMinutes++) {
        const timeOfDay = minutesToTimeOfDay(timeInMinutes)
        const presence = szenario[timeOfDay]
        if (presence !== undefined) {
            variables['$SYS/presence'] = presence
        }
        szenario.date.setHours(0, timeInMinutes, 0, 0)
        // const processRule = new ProcessRule(variables, szenario.longitude, szenario.latitude, szenario.date)
        // const result = loopRules(processRule, rules, timeInMinutes, history)
        automation.variables = variables
        const result = automation.processRules(szenario.date)
        messages = [...messages, ...result.messages]
    }
    return messages
}

module.exports = testRules
