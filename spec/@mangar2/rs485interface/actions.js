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

const { Retry, types, errorLog } = require('@mangar2/utils')
const Message = require('@mangar2/message')
const ONE_SECOND = 1000

/**
 * Creates a class supporting message actions:
 * - blink (lets a lamp blink)
 * - temporary (switches on temporarily and automatically switches off)
 * - set set the value
 * @param {Object} options currently empty
 * @param {number} options.blinkDelayInSeconds delay between blinks
 * @param {number} options.temporaryOnInSeconds time switches are set to on temporarily by default
 * @private
 */
class Actions {
    constructor (options) {
        this._retry = new Retry()
        this._topics = {}
        this._options = options
    }

    /**
     * blinks an amount of times
     * Blinks reverse, if the device is currently on (off/on instead of on/off)
     * The device will be at the inital state after blinking
     * @param {string} topic topic of the message
     * @param {number} amount amount of blink cycles (if < 1 it will blink exactly once)
     * @param {string} state current state of the element to blink ("on" or "off")
     * @param {function} switchFunction function(newState) switching the device
     * @private
     */
    _blink (topic, amount, state, switchFunction) {
        const retryCnt = amount > 0 ? amount * 2 : 2
        let newState = state

        if (types.isFunction(switchFunction)) {
            this._retry.topicRetry(topic, retryCnt, this._options.blinkDelayInSeconds * ONE_SECOND, () => {
                newState = newState === 'on' ? 'off' : 'on'
                switchFunction(newState)
            })
        }
    }

    /**
     * Sets switches on temporarily
     * Enables a switch for some time and switch it of automatically
     * @param {string} topic topic of the message
     * @param {number} value value of the message
     * @param {function} switchFunction function(newState) switching the device
     * @private
     */
    _temporary (topic, value, switchFunction) {
        const retryCnt = 2
        let newState = 'on'
        const delayTimeInSeconds = types.isInteger(value) ? value : this._options.temporaryOnInSeconds

        if (types.isFunction(switchFunction)) {
            this._retry.topicRetry(topic, retryCnt, delayTimeInSeconds * ONE_SECOND, () => {
                switchFunction(newState)
                newState = newState === 'on' ? 'off' : 'on'
            })
        }
    }

    /**
     * Switches on directly. Terminates all other loops like blink or temporarly.
     * Example: A switch is set ot on temporarly for 10 minutes. After 5 minutes the switch is
     * "set" to on. Then the automatic switch off of the temporary call is disabled
     * @param {string} topic topic of the message
     * @param {function} switchFunction function(newState) switching the device
     * @private
     */
    _set (topic, switchFunction) {
        const retryCnt = 1
        if (types.isFunction(switchFunction)) {
            this._retry.topicRetry(topic, retryCnt, 3 * ONE_SECOND, () => {
                switchFunction()
            })
        }
    }

    /**
     * Processes a mqtt message
     * @param {Message} commandMessage message to process
     * @param {SendCallback} switchFunction function processing the message
     */
    processMessage (commandMessage, sendMessage) {
        try {
            const message = new Message(commandMessage.topic, commandMessage.value, commandMessage.reason)
            message.topic = message.topic.toLowerCase()
            if (message.topic.endsWith('/set')) {
                message.topic = message.topic.replace('/set', '')
                this._set(message.topic, () => {
                    sendMessage(message)
                })
            } else if (message.topic.endsWith('/temporary')) {
                message.topic = message.topic.replace('/temporary', '')
                this._temporary(message.topic, message.value, newValue => {
                    message.value = newValue
                    sendMessage(message)
                })
            } else if (message.topic.endsWith('blink')) {
                message.topic = message.topic.replace('/blink', '')
                const curState = this._topics[message.topic]
                this._blink(message.topic, message.value, curState, newValue => {
                    message.value = newValue
                    sendMessage(message)
                })
            } else {
                throw Error('topic with unknown string end (/set, /temporary or /blink expected) ' + message.topic)
            }
        } catch (err) {
            errorLog(err)
        }
    }

    /**
     * Stores the current state reported by messages
     * @param {Message[]} mqttMessages list of mqtt messages
     */
    storeState (mqttMessages) {
        for (const message of mqttMessages) {
            this._topics[message.topic] = message.value
        }
    }
}

module.exports = Actions
