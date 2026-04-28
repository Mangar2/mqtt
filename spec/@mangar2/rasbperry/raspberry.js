/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Provides a service to invoke broker messages from external networks. Allows only configured messages.
 */

'use strict'

const { errorLog, Callbacks } = require('@mangar2/utils')
const sanitizeConfiguration = require('@mangar2/rasbperry/configuration')
const messageToGpio = require('./messagetogpio')
const Message = require('@mangar2/message')

const Gpio = require('onoff').Gpio 

const DEBUG = true

/**
 * Creates a remote service
 * @param {Object} [options={}] service configuration
 * @param {Service[]} options.switches gpio switch configuration
 */
class Raspberry {
    constructor (options = {}) {
        this._options = sanitizeConfiguration(options)
        this._callbacks = new Callbacks(['publish'])
    }

    /**
     * Sets a callback.
     * @param {string} event event name (not case sensitive) for the callback
     * @param {function} callback function(...parameter)
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) {
        this._callbacks.on(event, callback)
    }

    /**
     * 
     * @param {number} gpioNo number of the gpio to set
     * @param {0|1} value new status of the pin
     */
    _setGpio(gpioNo, value) {
        const pin = new Gpio(gpioNo, 'out') //use GPIO pin 4, and specify that it is output
        pin.writeSync(value)
        pin.unexport()
    }

    /**
     * Processes an incoming mqtt message
     * @param {Message} message mqtt message
     */
    handleMessage (message) {
        const { topic, value, reason } = message
        topic.replace('/set', '')
        let mqttMessage = new Message(topic, value, reason)

        try {
            mqttMessage.addReason('executed by raspberry service')
            const gpioCommand = messageToGpio(message, this._options)
            this._setGpio(gpioCommand.gpio, gpioCommand.value)
        } catch (err) {
            mqttMessage = new Message('$SYS/raspberry/error', 'configuration', JSON.stringify(err))
            errorLog(err, DEBUG)
        }
        return mqttMessage
    }
    
    /**
     * 
     */
    async close () {
    }
}

module.exports = Raspberry
