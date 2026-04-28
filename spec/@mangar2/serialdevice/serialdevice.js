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

const DEBUG = true
const { TaskQueue, Callbacks, errorLog, delay } = require('@mangar2/utils')
const { SerialConnection } = require('@mangar2/serialhelper')
const MatchMessages = require('@mangar2/matchmessages')

const { deriveSubscribes } = require('./derivesubscribes')
const sanitizeConfiguration = require('./configuration')
const MQTTToSerial = require('./mqtttoserial')
const { serialMessageToString } = require('./serialmessagetostring')

const SerialToMQTT = require('./serialtomqtt')
const ParseSerialData = require('./parseserialdata')
const Message = require('@mangar2/message')

/**
 * Callback to publish messages to the mqtt broker
 * @callback Publish
 * @param {Message} message message to publish
 */

/**
 * Creates a service class to communicate with several arduino over a serial bus
 * @param {Object} options configuration options
 * @param {string} options.serialPortName name of the port to use
 * @param {integer} [options.baudrate=57600] baud rate to be used
 * @param {0|1|2} [options.qos=1] quality of service to send messages
 * @param {string} [options.trace='messages'] trace level, supported: errors, messages, internal
 * @param {number} [options.keepAliveDelayInSeconds=1] delay between two keep alive messages to serial
 */
class SerialDevice {
    constructor (options) {
        this._options = sanitizeConfiguration(options)
        this._callbacks = new Callbacks(['publish'])
        this._matchMessages = new MatchMessages()
        this._serial = new SerialConnection()
        this._taskQueue = new TaskQueue(100 /* delay in ms between two tasks */)
        this._taskQueue.on('task', async (data) => this._sendDataToSerial(data))

        this._mqttToSerial = new MQTTToSerial(this._options)
        this._parseSerialData = new ParseSerialData()
        this._serialToMqtt = new SerialToMQTT(this._options)
        this._close = false
    }

    /**
     * Sets a callback.
     * @param {string} event event name (not case sensitive) for the callback (supported: 'publish')
     * @param {Publish} callback function(...parameter)
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) {
        this._callbacks.on(event, callback)
    }

    /**
     * Publishes mqtt messages to the broker
     * @param {Message[]} mqttMessages messages to publish
     * @private
     */
    _publish (mqttMessages) {
        for (let message of mqttMessages) {
            const isSetMessage = message.topic.endsWith('/set')
            // Reply messages are never set messages. Thus set is removed
            message.topic = message.topic.replace(new RegExp('/set$'), '')
            if (this._matchMessages.hasMatchingMessage(message)) {
                message = this._matchMessages.matchAndUpdateReplyMessage(message)
            } else if (isSetMessage) {
                // Keep the set, only if it is not a reply message
                message.topic += '/set'
            }
            message.qos = this._options.qos
            this._callbacks.invokeCallback('publish', message)
        }
    }

    /**
     * Processes a received serial message
     * @param {string} serialData data received from serial interface
     * @private
     */
    _processReceivedData (serialData) {
        try {
            const serialMessage = this._parseSerialData.parse(serialData)
            this._traceInfo(serialData, serialMessage)
            if (serialMessage) {
                const mqttMessages = this._serialToMqtt.toMqttMessages(serialMessage)
                this._publish(mqttMessages)
            }
        } catch (err) {
            errorLog(err, DEBUG)
        }
    }

    /**
     * Traces serial input
     * @param {Object} receivedObject
     * @private
     */
    _traceInfo (serialData, serialMessage) {
        const { trace } = this._options
        if (trace === 'internal') {
            const now = new Date()
            console.log('%s data: %s', now.toLocaleTimeString(), serialData.toString())
        }
        if (serialMessage && (trace === 'internal' || trace === 'messages')) {
            const now = new Date()
            console.log('%s serial -> %s', now.toLocaleTimeString(), serialMessage.toString())
        }
    }

    /**
     * Traces incoming messages
     * @param {Message} message mqtt message
     * @private
     */
    _traceIncomingMessage (message) {
        const { trace } = this._options
        if (message && (trace === 'internal' || trace === 'messages')) {
            const now = new Date()
            console.log('%s received: %s=%s',
                now.toLocaleTimeString(), message.topic, message.value)
        }
    }

    /**
     * Traces a string send to the serial device
     * @param {string} serialString string send to serial device
     */
    _traceStringSendToSerial (serialString) {
        const { trace } = this._options
        if (serialString && trace === 'internal') {
            const now = new Date()
            console.log('%s %s -> serial', now.toLocaleTimeString(), serialString)
        }
    }

    /**
     * Sends data to a serial device
     * @param {string} serialString array of bytes
     * @private
     */
    async _sendDataToSerial (serialString) {
        let retry = 3
        while (retry > 0) {
            try {
                this._traceStringSendToSerial(serialString)
                await this._serial.sendData(serialString)
                retry = 0
            } catch (err) {
                errorLog(err, DEBUG)
                if (retry === 0) {
                    throw err
                }
                await this._openSerialInterface()
                retry--
            }
        }
    }

    /**
     * Opens the serial interface
     * @private
     */
    async _openSerialInterface () {
        const RETRY = 10
        const DELAY_FOR_RETRY_IN_SECONDS = 15
        let loop = 1
        if (this._isSerialInterfaceOpen) {
            await this._serial.close()
            this._isSerialInterfaceOpen = false
        }
        while (!this._isSerialInterfaceOpen) {
            try {
                await this._serial.open(this._options.serialPortName, this._options.baudrate)
                this._isSerialInterfaceOpen = true
            } catch (err) {
                errorLog(err, DEBUG)
                await this._serial.listAvailablePorts()
                console.log('Retry in %s seconds %s/%s', DELAY_FOR_RETRY_IN_SECONDS, loop, RETRY)
                loop++
                if (loop >= RETRY) {
                    throw err
                }
                await delay(DELAY_FOR_RETRY_IN_SECONDS * 1000)
            }
        }
    }

    /**
     * Sends a serial keep alive message
     * @private
     */
    async _runSendKeepAlive () {
        while (!this._close) {
            try {
                await this._sendDataToSerial('at')
            } catch (err) {
                errorLog(err, DEBUG)
            }
            await delay(this._options.keepAliveDelayInSeconds * 1000)
        }
    }

    /**
     * Starts the server
     */
    async run () {
        this._serial.on('data', (serialData) => {
            this._processReceivedData(serialData)
        })
        try {
            await this._openSerialInterface()
            console.log('Serial service is running')
        } catch (err) {
            errorLog(err, DEBUG)
            this.close()
        }
        this._runSendKeepAlive()
    }

    /**
     * Stops the service
     */
    async close () {
        this._close = true
        try {
            await this._serial.close()
        } catch (err) {
            errorLog(err, DEBUG)
        }
        console.log('serial device service closed')
    }

    /**
     * Processes an incoming mqtt message
     * @param {Message} mqttMessage mqtt message
     */
    handleMessage (message) {
        try {
            const mqttMessage = new Message(message.topic, message.value, message.reason)
            this._traceIncomingMessage(mqttMessage)
            if (mqttMessage.topic === '$SYS/serialdevice/trace/set') {
                this._options.trace = mqttMessage.value
            } else {
                mqttMessage.addReason('received by serialDevice interface service')
                this._matchMessages.addReceivedMessage(mqttMessage)
                mqttMessage.topic = mqttMessage.topic.replace(new RegExp('/set$'), '')
                const serialMessage = this._mqttToSerial.toSerialMessage(mqttMessage)
                const serialString = serialMessageToString(serialMessage)
                this._taskQueue.addTask(serialString)
            }
        } catch (err) {
            errorLog(err, DEBUG)
        }
    }

    /**
     * Gets the list of required subscriptions for this service
     * @returns { topic: qos } list of subscription strings
     */
    getSubscriptions () {
        return deriveSubscribes(this._options)
    }
}

module.exports = SerialDevice
