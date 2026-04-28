/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * History entry for data with same value and reason coming in regular intervals.
 * These values are counted and not stored separately
 */

/**
 * History entry holding several identical values with the same reason and regular updates
 * @typedef {Object} IntervalEntry
 * @property {'interval'} type type of the entry
 * @property {number} amount amount of entries
 * @property {number} value value of the entry
 * @property {Object[]} reason reason of the oldest entry
 * @property {timestamp} firstTime timestamp of the oldes entry
 * @property {timestamp} lastTime timestamp of the newest entry
 */

/**
 * @private
 * Creates an History entry holding several identical values with the same reason and regular updates
 * @param {number} value value of the entry
 * @param {Object[]} reason reason of the oldest entry
 * @param {timestamp} firstTime timestamp of the oldes entry
 * @param {timestamp} [lastTime=0] timestamp of the newest entry
 * @param {number} [amount=0] amount of entries
 * @returns IntervalEntry
 */
function createIntervalEntry (value, reason, firstTime, lastTime = 0, amount = 0) {
    return {
        type: 'interval',
        amount,
        value,
        reason,
        firstTime,
        lastTime
    }
}

/**
 * Creates an History entry holding several identical values with same reason and regular updates
 * by a time entry.
 * @param {TimeEntry} timeEntry entry of type 'time'
 */
function createIntervalEntryFromTimeEntry (timeEntry) {
    return createIntervalEntry(
        timeEntry.value,
        timeEntry.reason,
        timeEntry.timestamps[0],
        timeEntry.timestamps[timeEntry.timestamps.length - 1],
        timeEntry.timestamps.length)
}

/**
 * @private
 * @description adds an element to an interval entry
 * @param {IntervalEntry} intervalEntry interval entry to add the entry
 * @param {Entry} entryToAdd entry to add to the first entry
 * @returns {IntervalEntry} firstEntry with one more element pushed to its timestamps array
 */
function addToIntervalEntry (intervalEntry, entryToAdd) {
    intervalEntry.lastTime = entryToAdd.time
    intervalEntry.amount++
    return intervalEntry
}

/**
 * @private
 * @description Decides, if a new interval matches the former intervals by calculating an upper and a lower
 * bound
 * @param {number} newInterval time interval between the last two entries
 * @param {number} joinedInterval time interval of already joined entries
 * @param {Object} options options to calculate the bounds
 * @param {number} options.upperBoundFactor factor to multiply the joined interval for the upper bound
 * @param {number} options.upperBoundAddInMilliseconds constant to add to the joined interval for the upper bound
 * @param {number} options.lowerBoundFactor factor to multiply the joined interval for the lower bound
 * @param {number} options.lowerBoundSubInMilliseconds constant to sub from the joined interval for the lower bound
 */
function isMatchingInterval (newInterval, joinedInterval, options) {
    const belowUpperBound = newInterval < joinedInterval * options.upperBoundFactor + options.upperBoundAddInMilliseconds
    const aboveLowerBound = newInterval > joinedInterval * options.lowerBoundFactor - options.lowerBoundSubInMilliseconds
    return belowUpperBound && aboveLowerBound
}

/**
 * @private
 * @description checks if a new entry matches the interval entry (has same interval)
 * @param {IntervalEntry} joinedEntry first entry
 * @param {Entry} entryToAdd entry to add to the first entry
 * @param {Options} options options to calculate the bounds
 * @returns {{type, values, reason}} firstEntry with one more element pushed to its timestamps array
 */
function isMatchingIntervalEntry (joinedEntry, entryToAdd, options) {
    const sameValue = joinedEntry.value === entryToAdd.value
    let result = false
    if (sameValue) {
        const joinedInterval = (joinedEntry.lastTime - joinedEntry.firstTime) / (joinedEntry.amount - 1)
        const newInterval = entryToAdd.time - joinedEntry.lastTime
        result = isMatchingInterval(newInterval, joinedInterval, options)
    }
    return result
}

/**
 * Checks, if a time entry can be converted to a interval entry
 * @param {TimeEntry} timeEntry
 * @param {Object} options options to calculate the bounds
 * @param {number} options.upperBoundFactor factor to multiply the joined interval for the upper bound
 * @param {number} options.upperBoundAddInMilliseconds constant to add to the joined interval for the upper bound
 * @param {number} options.lowerBoundFactor factor to multiply the joined interval for the lower bound
 * @param {number} options.lowerBoundSubInMilliseconds constant to sub from the joined interval for the lower bound
 * @returns {boolean} true, if converting is possible
 */
function isIntervalEntry (timeEntry, options) {
    let result = true
    const firstTime = timeEntry.timestamps[0]
    const amount = timeEntry.timestamps.length
    const lastTime = timeEntry.timestamps[amount - 1]
    const joinedInterval = (lastTime - firstTime) / (amount - 1)
    for (let index = 1; index < amount; index++) {
        const newInterval = timeEntry.timestamps[index] - timeEntry.timestamps[index - 1]
        if (!isMatchingInterval(newInterval, joinedInterval, options)) {
            result = false
            break
        }
    }
    return result
}

/**
 * Checks, if last entries in a timeEntry can be converted to an interval entry
 * @param {TimeEntry} timeEntry
 * @param {Options} options options to calculate the bounds
 * @returns {IntervalEntry} amount of last entries that can be converted
 */
function getLastIntervalEntries (timeEntry, options) {
    const entryAmount = timeEntry.timestamps.length
    const result = createIntervalEntry(
        timeEntry.value,
        timeEntry.reason,
        0,
        timeEntry.timestamps[entryAmount - 1]
    )
    const amount = options.lengthForFurtherCompression
    if (entryAmount <= amount) {
        return result
    }
    result.firstTime = timeEntry.timestamps[entryAmount - amount]
    const expectedInterval = (result.lastTime - result.firstTime) / (amount - 1)
    for (let index = entryAmount - 1; index > 0; index--) {
        result.firstTime = timeEntry.timestamps[index - 1]
        const newInterval = timeEntry.timestamps[index] - timeEntry.timestamps[index - 1]
        if (!isMatchingInterval(newInterval, expectedInterval, options)) {
            break
        }
        result.amount++
    }
    return result
}

/**
 * @private
 * @description adds an element to a time entry
 * @param {TimeEntry} timeEntry first entry
 * @param {Options} options options to calculate the bounds
 * @returns {TimeEntry|IntervalEntry} either original TimeEntry or converted entry
 */
function tryConvertToIntervalEntry (timeEntry, options) {
    let result = timeEntry
    if (isIntervalEntry(timeEntry, options)) {
        result = createIntervalEntryFromTimeEntry(timeEntry)
    }
    return result
}

module.exports = {
    createIntervalEntry,
    createIntervalEntryFromTimeEntry,
    isMatchingIntervalEntry,
    addToIntervalEntry,
    isIntervalEntry,
    getLastIntervalEntries,
    tryConvertToIntervalEntry
}
