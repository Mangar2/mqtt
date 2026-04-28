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
 * Maximal lenght of the motion history list
 * @private
 */
const MAX_HISTORY_LENGTH = 100

/**
 * Hysterese to delete old motion entries
 * @private
 */
const HISTORY_LENGTH_HYSTERESE = 20

/**
 * Creates a motion history object. It stores motion messages in a list and
 * provides the function to get a list of recent motions.
 */
class EventHistory {
    constructor () {
        this.clear()
    }

    /**
     * Adds a detected motion
     * @param {string} topic topic of the motion
     * @param {number} timestamp timestamp of the motion in milliseconds
     * @param {boolean} isMotion true, if it is a motion event
     * @private
     */
    _addEvent (topic, timestamp, isMotion) {
        if (isMotion) {
            const id = this._id
            this._id++
            this._motionEventList.push({ topic, timestamp, id })
            if (this._motionEventList.length > MAX_HISTORY_LENGTH) {
                this._motionEventList = this._motionEventList.slice(HISTORY_LENGTH_HYSTERESE)
            }
        } else {
            this._nonMotionEventList[topic] = true
        }
    }

    /**
     * Gets a displaystring for the latest movement room list
     * @param {Object} motions object with key/value where key is the name of the motion sensor and value the timestamp of last motion
     * @returns {string}
     * @private
     */
    _latestEventListToDisplayString (motions) {
        let displayString = ''
        let separator = ''
        for (const roomName in motions) {
            const latestRoomMotionAsLocalTimeString = (new Date(motions[roomName])).toLocaleTimeString()
            displayString = displayString + separator + roomName + ' ' + latestRoomMotionAsLocalTimeString
            separator = ', '
        }
        return displayString
    }

    /**
     * Deletes all event entries
     */
    clear () {
        this._id = 0
        this._motionEventList = []
        this._nonMotionEventList = {}
    }

    /**
     * Clears all non motion events
     */
    clearNonMotionEvents () {
        this._nonMotionEventList = {}
    }

    /**
     * Checks a message for motions and if a motion is included adds it to the motion list
     * @param {Message} message received message
     * @param {boolean} [isMotion] true, if the message is a motion message
     */
    addEvent (message, isMotion) {
        const { topic, value } = message
        if (value !== 0) {
            const time = message.getDateOfNewestChange()
            const timestamp = time ? time.getTime() : undefined
            this._addEvent(topic, timestamp, isMotion)
        }
    }

    /**
     * Gets the last motion and all "related" motions. A motion is "related", if it happend nearly at
     * the same time
     * @param {number} [relatedMotionTimespanInSeconds=5] time in seconds when a motion is a related motion
     * @returns {Object} { timestamp, motions, displayString }  timestamp is the latest motion, motions is an Object
     * containing a map of topic: timestamp and displayString is a string to visualize the motions in a log
     */
    getLatestEvents (relatedMotionTimespanInSeconds = 5) {
        const ONE_SECOND = 1000
        const relatedMotionTimespanInMilliseconds = relatedMotionTimespanInSeconds * ONE_SECOND
        const result = {
            timestamp: 0,
            motions: {},
            nonMotions: this._nonMotionEventList,
            displayString: ''
        }
        if (this._motionEventList.length > 0) {
            const latestMotion = this._motionEventList[this._motionEventList.length - 1]
            result.timestamp = latestMotion.timestamp
            const minimumTimestamp = result.timestamp - relatedMotionTimespanInMilliseconds
            for (let index = this._motionEventList.length - 1; index >= 0; index--) {
                const motion = this._motionEventList[index]
                if (motion.timestamp >= minimumTimestamp && result.motions[motion.topic] === undefined) {
                    result.motions[motion.topic] = motion.timestamp
                }
            }
            result.displayString = this._latestEventListToDisplayString(result.motions)
        }
        return result
    }
}

module.exports = EventHistory
