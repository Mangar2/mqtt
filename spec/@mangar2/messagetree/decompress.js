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

/**
 * @typedef {Object[]} Reason
 * @property {string} message description of the event reason
 * @property {string} time timestamp the event happened in ISO-Format
 */

/**
 * @typedef {Object} Decompressed
 * @property {number} value value of the entry
 * @property {string} time time the value was received (ISO-Format)
 * @property {Reason} [reason] array of reason information
 */

/**
 * @private
 * @description Decompresses an entry of type single
 * @param {Object} entry entry to decompress
 * @param {number} entry.value value of the entry
 * @param {time} entry.time timestamp of th entry
 * @param {Reason} [entry.reason] reason of the entry
 * @returns {Decompressed[]} entry in decompressed format
 */
function decompressSingle (entry) {
    const { value, reason } = entry
    const time = new Date(entry.time).toISOString()
    return [{ time, value, reason }]
}

/**
 * @private
 * @description Decompresses an entry of type timevalue
 * @param {Object} entry entry to decompress
 * @param {Array} entry.values array of time/value pairs
 * @param {Reason} [entry.reason] reason of the entry
 * @returns {Decompressed[]} entries in decompressed format
 */
function decompressTimeValue (entry) {
    const INDEX_TIME = 0
    const INDEX_VALUE = 1
    const result = []
    for (const value of entry.values) {
        result.unshift({ time: new Date(value[INDEX_TIME]).toISOString(), value: value[INDEX_VALUE] })
    }
    // The reason contains the timestamp matching of the oldest entry that is now at the end of the array
    result[result.length - 1].reason = entry.reason
    return result
}

/**
 * @private
 * @description Decompresses an entry of type time
 * @param {Object} entry entry to decompress
 * @param {number} entry.value value of the entry
 * @param {Array} entry.timestamps array of time/value pairs
 * @param {Reason} [entry.reason] reason of the entry
 * @returns {Decompressed[]} entries in decompressed format
 */
function decompressTime (entry) {
    const result = []
    const value = entry.value
    for (const time of entry.timestamps) {
        result.unshift({ time: new Date(time).toISOString(), value })
    }
    // The reason contains the timestamp matching of the oldest entry that is now at the end of the array
    result[result.length - 1].reason = entry.reason
    return result
}

/**
 * @private
 * @description Decompresses an entry of type interval - creating "amount" of separate entries
 * @param {Object} entry entry to decompress
 * @param {number} entry.value value of the entry
 * @param {number} entry.amount amount of entries
 * @param {number} entry.firstTime timestamp the value was encountered first
 * @param {number} entry.lastTime timestamp the value was encoutered last
 * @param {Reason} [entry.reason] reason of the entry
 * @returns {Decompressed[]} entries in decompressed format
 */
function decompressInterval (entry) {
    const value = entry.value
    const time = new Date(entry.firstTime).toISOString()
    const reason = [{ timestamp: time, message: 'regular update, amount: ' + entry.amount }, ...entry.reason]
    const result = [{ time, value, reason }]

    return result
}

/**
 * @description Decomresses a history entry to an external format
 * @param {Object[]} history history list
 * @param {string} [history[].type] type of the entry ('single', 'timeValue', 'time', 'interval)
 * @param {number} [history[].time] timestamp of the second entry (only type 'single')
 * @param {number} [history[].value] value of the first entry (only types 'single', 'time', 'interval')
 * @param {Object[]} history[].reason list of reasons
 * @param {Object[]} [history[].values] array of [time, value] (only type 'timeValue')
 * @param {number[]} [history[].timestamps] array of timestamps (only type 'time')
 * @param {number} [history[].amount] amount of entries joined before (only type 'interval')
 * @param {number} [history[].firstTime] timestamp the value was encountered first (only type 'interval')
 * @param {number} [history[].lastTime] timestamp the value was encoutered last (only type 'interval')
 * @returns {Decompressed[]} array of decompressed history entries
 */
function decompress (history) {
    const result = []
    for (const entry of history) {
        switch (entry.type) {
        case 'single':
            result.push(...decompressSingle(entry))
            break
        case 'timeValue':
            result.push(...decompressTimeValue(entry))
            break
        case 'time':
            result.push(...decompressTime(entry))
            break
        case 'interval':
            result.push(...decompressInterval(entry))
            break
        }
    }
    return result
}

module.exports = decompress
