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
const DEBUG = false
const { Callbacks, errorLog, delay, types } = require('@mangar2/utils')
const { BROADCAST_ADDRESS } = require('./constants')
const { SerialConnection } = require('@mangar2/serialhelper')
const SerialMessage = require('./serialmessage')
const ReadMessages = require('./readmessages')
const SerialDNS = require('./serialdns')
const RS485Schedule = require('./rs485schedule')
const sanitizeConfiguration = require('./configuration')
const Actions = require('./actions')
const deriveSubscribes = require('./derivesubscribes')
const MatchMessages = require('@mangar2/matchmessages')

/**
 * Callback to publish messages to the mqtt broker
 * @callback Publish
 * @param {Message} message message to publish
 */

/**
 * Creates a service class to communicate with several arduino over a RS485 bus
 * Most options are preset (see "configuration.js" for all presets). You must provide
 * the serial port name in operating system format and a list of addresses as object
 * @example
 * const serialPortName = 'COM1'
 * // Do not use 0 or 1 as addresses. 0 is used for broadcast events and 1 is the
 * // default address of this service.
 * const addresses = {
 *    myFloor/myRoom/myDevice: 2,
 *    myOtherFloor/myOtherRoom/myDevice: 3
 *    ...
 * }
 * const arduinoService = new ArduinoRS485({ serialPortName, addresses })
 * @param {Object} options configuration options
 * @param {string} options.serialPortName name of the port to use
 * @param {integer} [options.baudrate=57600] baud rate to be used
 * @param {integer} [options.myAddress=1] address of this rs485 server
 * @param {integer} [options.maxVersion=1] maximal suported interface version
 * @param {integer} [options.tickDelay=100] inner clock managing the token, every action is calculated
 * @param {integer} [options.timeOfDayDelayInSeconds=60] delay between sending the time of day
 * @param {0|1|2} [options.qos=1] quality of service to send messages
 * @param {string} [options.trace='messages'] trace level, supported errors, messages, token
 */
class RS485Interface {
    constructor (options) {
        options = sanitizeConfiguration(options)
        this._serial = new SerialConnection()
        this._callbacks = new Callbacks(['publish'])
        this._read = new ReadMessages()
        this._schedule = new RS485Schedule(options)
        this._arduinoDNS = new SerialDNS(options)
        this._actions = new Actions(options)
        this._matchMessages = new MatchMessages()
        this._options = options
        this._timeOfDayDelayInSeconds = options.timeOfDayDelayInSeconds
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
            message = this._matchMessages.matchAndUpdateReplyMessage(message)
            message.qos = this._options.qos
            this._callbacks.invokeCallback('publish', message)
        }
    }

    /**
     * Processes a received serial message
     * @param {SerialMessage} serialMessage message received
     * @private
     */
    _processReceivedSerialMessage (serialMessage) {
        try {
            if (serialMessage) {
                const sendToBroker = this._schedule.processReceivedMessage(serialMessage)
                if (sendToBroker) {
                    const mqttMessages = this._arduinoDNS.toMqttMessages(serialMessage)
                    this._actions.storeState(mqttMessages)
                    this._publish(mqttMessages)
                }
            }
        } catch (err) {
            errorLog(err, DEBUG)
        }
    }

    /**
     * Traces serial input
     * @param {Object} serialInfo
     * @param {SerialMessage} serialInput.message message read
     * @param {string} serialInput.error error string
     * @private
     */
    _traceInfo (serialInfo) {
        const { message, error } = serialInfo
        const { trace } = this._options
        if (error && ( trace === 'error' || trace === 'messages' || trace === 'internal')) {
            console.log(error)
        }
        if (types.isObject(message) && message !== null) {
            if ((trace === 'messages' && message.isInternal() === false) || trace === 'internal') {
                console.log(message.getLoggingInfo())
            }
        }
    }

    /**
     * Sends data to a serial device
     * @param {Array} byteArray array of bytes
     * @private
     */
    async _sendDataToSerial (byteArray) {
        let retry = 3
        while (retry > 0) {
            try {
                await this._serial.sendData(byteArray)
                retry = 0
            } catch (err) {
                errorLog(err, DEBUG)
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
     * Starts the server
     */
    async run () {
        this._serial.on('data', (byteArray) => {
            const readResult = this._read.read(byteArray)
            for (const result of readResult) {
                this._traceInfo(result)
                this._processReceivedSerialMessage(result.message)
            }
        })
        try {
            await this._openSerialInterface()
            this._schedule.on('send', async (byteArray) => {
                return this._sendDataToSerial(byteArray)
            })
            this._schedule.on('trace', serialInfo => {
                this._traceInfo(serialInfo)
            })
            this._runSendTimeOfDay()
            this._schedule.run()
            console.log('rs485 service is running')
        } catch (err) {
            errorLog(err, DEBUG)
            this.close()
        }
    }

    /**
     * Sends a serial message with the time of day as broadcast in a loop
     * @private
     */
    async _runSendTimeOfDay () {
        while (!this._close) {
            try {
                const message = new SerialMessage()
                message.sender = this._options.myAddress
                message.reply = false
                message.receiver = BROADCAST_ADDRESS
                message.command = 'C'
                const date = new Date()
                message.value = date.getHours() * 60 + date.getMinutes()
                this._schedule.sendMessage(message)
            } catch (err) {
                errorLog(err, DEBUG)
            }
            await delay(this._timeOfDayDelayInSeconds * 1000)
        }
    }

    /**
     * Stops the service
     */
    async close () {
        this._close = true
        try {
            await this._schedule.close()
            await this._serial.close()
        } catch (err) {
            errorLog(err, DEBUG)
        }
        console.log('rs485 service closed')
    }

    /**
     * Processes an incoming mqtt message
     * @param {Message} message mqtt message
     */
    handleMessage (message) {
        try {
            if (message.topic === '$SYS/rs485Interface/trace/set') {
                this._options.trace = message.value
            } else {
                message.addReason('received by RS485Interface service')
                this._matchMessages.addReceivedMessage(message)
                this._actions.processMessage(message, (actionMessage) => {
                    try {
                        const serialMessage = this._arduinoDNS.toSerialMessage(actionMessage)
                        serialMessage.reply = true
                        this._schedule.sendMessage(serialMessage)
                    } catch (err) {
                        errorLog(err, DEBUG)
                    }
                })
            }
        } catch (err) {
            errorLog(err, DEBUG)
        }
    }

    /**
     * Gets the list of required subscriptions for this service
     * @returns {{topic: string, qos: number}[]} list of subscription strings
     */
    getSubscriptions () {
        return deriveSubscribes(this._options)
    }
}

module.exports = RS485Interface
