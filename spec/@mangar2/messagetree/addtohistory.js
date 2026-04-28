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

const {
    isMatchingIntervalEntry,
    tryConvertToIntervalEntry,
    getLastIntervalEntries,
    addToIntervalEntry
} = require('./intervalentry')

const { getIdenticalValueTimeList } = require('./compress.js')

const TIME_VALUE_VALUE_INDEX = 1

/**
 * Entry to add to the history
 * @typedef {Object} Entry
 * @property {timestamp} time timestamp of the entry
 * @property {Any} value message value
 * @property {Object[]} reason message trace information
 */

/**
 * History entry holding a single value
 * @typedef {Object} SingleEntry
 * @property {'single'} type type of the entry
 * @property {timestamp} time timestamp of the entry
 * @property {Any} value value of the entry
 * @property {Object[]} reason reason of the entry
 */

/**
 * History entry holding several different values with the same reason
 * @typedef {Object} TimeValueEntry
 * @property {'timeValue'} type type of the entry
 * @property {{time: number, value: Any}[]} values time/value pairs
 * @property {Object[]} reason reason of the oldest entry
 */

/**
 * History entry holding several identical values with the same reason
 * @typedef {Object} TimeEntry
 * @property {'time'} type type of the entry
 * @property {number[]} timestamps timestampls of the entries
 * @property {Object[]} reason reason of the oldest entry
 */

/**
 * @typedef {SingleEntry|TimeValueEntry|TimeEntry|IntervalEntry} HistoryEntry
 */

/**
 * Options to manage the message tree and its history
 * @typedef {Object} Options
 * @property {number} upperBoundFactor factor to multiply the joined interval for the upper bound
 * @property {integer} upperBoundAddInMilliseconds constant to add to the joined interval for the upper bound
 * @property {number} lowerBoundFactor factor to multiply the joined interval for the lower bound
 * @property {integer} lowerBoundSubInMilliseconds constant to sub from the joined interval for the lower bound
 * @property {integer} maxHistoryLength maximal amount of entries in the history
 * @property {integer} historyHysterese amount of entries to delete, if history exeeds max length
 * @property {integer} maxValuesPerHistoryEntry maximum amount of values per history entry
 * @property {integer} lengthForFurtherCompression history timeValue length to look for additional compression
 */

/**
 * @private
 * @param {Reason} reason1
 * @param {Reason} reason2
 * @description Checks, if two reasons are similar
 */
function isReasonSimilar (reason1, reason2) {
    const sameAmountOfReasons = Array.isArray(reason1) && Array.isArray(reason2) && reason1.length === reason2.length
    const reasonsUndefined = reason1 === undefined && reason2 === undefined
    let isSimilar = sameAmountOfReasons || reasonsUndefined
    if (sameAmountOfReasons) {
        for (const index in reason1) {
            const message1 = reason1[index].message
            const message2 = reason2[index].message
            isSimilar = message1 === message2
            if (!isSimilar) break
        }
    }
    return isSimilar
}

/**
 * @private
 * @description Checks, if two entries are similar. They are similar, if the reasons are similar and the values are
 * identical
 */
function isEntrySimilar (entry1, entry2) {
    const entriesAreValid = entry1 !== undefined && entry1 !== null & entry2 !== undefined && entry2 !== null
    const reasonsAreSimilar = entriesAreValid && isReasonSimilar(entry1.reason, entry2.reason)
    return reasonsAreSimilar
}

/**
 * @private
 * @description Creates a time/value entry for the history
 * @param {SingleEntry} firstEntry first entry
 * @param {Entry} entryToAdd entry to add to the first entry
 * @returns {TimeValueEntry} Object with array of values[[time, value], [time, value]] and reason
 */
function createTimeValueEntry (firstEntry, entryToAdd) {
    const result = {
        type: 'timeValue',
        values: [[firstEntry.time, firstEntry.value], [entryToAdd.time, entryToAdd.value]],
        reason: firstEntry.reason
    }
    return result
}

/**
 * @private
 * @description adds an element to a time/value entry
 * @param {TimeValueEntry} timeValueEntry first entry
 * @returns {TimeValeEntry|TimeEntry} either TimeValueEntry or converted to TimeEntry
 */
function tryConvertToTimeEntry (timeValueEntry) {
    let result = timeValueEntry
    const length = timeValueEntry.values.length
    const value = timeValueEntry.values[length - 1][TIME_VALUE_VALUE_INDEX]
    const timestamps = getIdenticalValueTimeList(timeValueEntry)
    const areAllValuesIdentical = (timestamps.length === length)
    if (areAllValuesIdentical) {
        result = { type: 'time', value, timestamps, reason: timeValueEntry.reason }
    }
    return result
}

/**
 * @private
 * @description tries to compress a time/value entry either to a "time only array" or a "just interval" entry
 * @param {TimeValueEntry} timeValueEntry first entry
 * @param {Options} options options to calculate the bounds
 * @returns {TimeValueEntry|TimeEntry|IntervalEntry} original TimeValueEntry or converted entry
 */
function tryToCompress (timeValueEntry, options) {
    let result = timeValueEntry
    if (timeValueEntry.values.length === options.lengthForFurtherCompression) {
        result = tryConvertToTimeEntry(timeValueEntry)
        if (result.type === 'time') {
            result = tryConvertToIntervalEntry(result, options)
        }
    }
    return result
}

/**
 * @private
 * @description Adds an element to the history array. It will be added as first element
 * @param {Object[]} history array of elements
 * @param {Object} entryToAdd entry to add to the history entry
 * @returns {Object[]} new history with added element
 */
function addNewElementToHistory (history, entryToAdd) {
    entryToAdd.type = 'single'
    history.unshift(entryToAdd)
    return history
}

/**
 * @private
 * @description adds an element to a time/value entry
 * @param {TimeValueEntry} firstEntry first entry
 * @param {Entry} entryToAdd entry to add to the first entry
 * @returns {TimeValueEntry} firstEntry with one more element pushed to its values entry
 */
function addToTimeValueEntry (firstEntry, entryToAdd) {
    firstEntry.values.push([entryToAdd.time, entryToAdd.value])
    return firstEntry
}

/**
 * @private
 * @description adds an element to a time entry
 * @param {TimeEntry} joinedEntry first entry
 * @param {Entry} entryToAdd entry to add to the first entry
 * @returns {TimeEntry} firstEntry with one more element pushed to its timestamps array
 */
function addToTimeEntry (joinedEntry, entryToAdd) {
    joinedEntry.timestamps.push(entryToAdd.time)
    return joinedEntry
}

/**
 * @private
 * @description Truncates the history array, if it is too large.
 * @param {Object[]} history array of elements
 * @param {Options} options options
 * @returns {Object[]} possible truncated history
 */
function truncateHistoryIfTooLarge (history, options) {
    if (history.length >= options.maxHistoryLength) {
        let newLength = options.maxHistoryLength - options.historyHysterese
        if (newLength <= 0) {
            newLength = 1
        }
        history = history.slice(0, newLength)
    }
    return history
}

/**
 * Check if the current history entry has space left to add one more element
 * @param {HistoryEntry} historyEntry History entry to check
 * @param {Options} options configuration options
 * @returns {boolean} true, if the history entry accepts more values
 * @private
 */
function isSpaceLeft (historyEntry, options) {
    let result = true
    const maxValues = options.maxValuesPerHistoryEntry
    switch (historyEntry.type) {
    case 'single':
        result = maxValues > 1
        break
    case 'timeValue':
        result = historyEntry.values.length < maxValues
        break
    case 'time':
        result = historyEntry.timestamps.length < maxValues
        break
    default:
        // leave result unchanged true
    }
    return result
}

/**
 * @description Checks to split the last history timeValue entry
 * @param {HistoryEntry[]} history history list
 * @param {Options} options options to calculate the bounds
 * @returns {HistoryEntry[]} updated history
 */
function splitTimeValueEntryIfAppropriate (history, options) {
    if (history[0].type === 'timeValue') {
        const timestamps = getIdenticalValueTimeList(history[0])
        const length = timestamps.length
        if (length >= options.lengthForFurtherCompression) {
            const curLength = history[0].values.length
            const value = history[0].values[curLength - 1][TIME_VALUE_VALUE_INDEX]
            const reason = history[0].reason
            history[0].values = history[0].values.slice(0, history[0].values.length - length)
            let newEntry = { type: 'time', value, timestamps, reason }
            newEntry = tryConvertToIntervalEntry(newEntry, options)
            history.unshift(newEntry)
        }
    }
}

/**
 * @description Checks to split the last history time entry
 * @param {HistoryEntry[]} history history list
 * @param {Options} options options to calculate the bounds
 * @returns {HistoryEntry[]} updated history
 */
function splitTimeEntryIfAppropriate (history, options) {
    const intervalEntry = getLastIntervalEntries(history[0], options)
    if (intervalEntry.amount >= options.lengthForFurtherCompression) {
        history[0].timestamps = history[0].timestamps.slice(
            0, history[0].timestamps.length - intervalEntry.amount)
        history.unshift(intervalEntry)
    }
}

/**
 * @description Adds an entry to the history and possible add it or join it with the latest history entry
 * @param {HistoryEntry[]} history history list
 * @param {Entry} entryToAdd entry to add to history
 * @param {Options} options options to calculate the bounds
 * @returns {HistoryEntry[]} updated history
 */
function addToHistory (history, entryToAdd, options) {
    if (history.length === 0) {
        history = addNewElementToHistory(history, entryToAdd)
    } else if (!isEntrySimilar(entryToAdd, history[0])) {
        history = addNewElementToHistory(history, entryToAdd)
    } else if (!isSpaceLeft(history[0], options)) {
        history = addNewElementToHistory(history, entryToAdd)
    } else {
        switch (history[0].type) {
        case 'single':
            history[0] = createTimeValueEntry(history[0], entryToAdd)
            break
        case 'timeValue':
            history[0] = addToTimeValueEntry(history[0], entryToAdd)
            history[0] = tryToCompress(history[0], options)
            splitTimeValueEntryIfAppropriate(history, options)
            break
        case 'time':
            if (history[0].value === entryToAdd.value) {
                history[0] = addToTimeEntry(history[0], entryToAdd)
                splitTimeEntryIfAppropriate(history, options)
            } else {
                history = addNewElementToHistory(history, entryToAdd)
            }
            break
        case 'interval':
            if (isMatchingIntervalEntry(history[0], entryToAdd, options)) {
                history[0] = addToIntervalEntry(history[0], entryToAdd)
            } else {
                history = addNewElementToHistory(history, entryToAdd)
            }
            break
        }
    }
    history = truncateHistoryIfTooLarge(history, options)

    return history
}

module.exports = addToHistory
