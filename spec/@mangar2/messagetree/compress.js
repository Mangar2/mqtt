/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * This file includes helper functions to compress history entries of messages
 */
'use strict'

/**
 * History entry holding several identical values with the same reason
 * @typedef {Object} TimeEntry
 * @property {'time'} type type of the entry
 * @property {number[]} timestamps timestampls of the entries
 * @property {Object[]} reason reason of the oldest entry
 */

/**
 * @private
 * @description adds an element to a time/value entry
 * @param {TimeValueEntry} timeValueEntry first history entry
 * @returns {Array} Array of time entries
 */
function getIdenticalValueTimeList (timeValueEntry) {
    const TIME_INDEX = 0
    const VALUE_INDEX = 1
    const length = timeValueEntry.values.length
    const value = timeValueEntry.values[length - 1][VALUE_INDEX]
    const timestamps = []
    for (let index = length - 1; index >= 0; index--) {
        const entry = timeValueEntry.values[index]
        if (entry[VALUE_INDEX] !== value) {
            break
        }
        timestamps.push(entry[TIME_INDEX])
    }
    return timestamps.reverse()
}

module.exports = {
    getIdenticalValueTimeList
}
