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

// path to your configuration file
const sanitizeConfiguration = require('@mangar2/zwave/configuration')
const ZwaveController = require('@mangar2/zwave/zwavecontroller')
const Callbacks = require('@mangar2/callbacks')
const MatchMessages = require('@mangar2/matchmessages')
const Message = require('@mangar2/message')
const errorLog = require('@mangar2/errorlog')
const Devices = require('@mangar2/devices')
const DEBUG = true

/**
 * Creates a remote service
 * @param {Object} options configuration settings for zwave
 * @param {string} options.usb zwave usb device name 
 * @param {Service[]} options.services list of supported services
 */
class ZwaveService {
    constructor (options = {}) {
        this._options = sanitizeConfiguration(options)
        this._callbacks = new Callbacks(['publish'])
        this._controller = new ZwaveController(this._options.usb)
        this._matchMessages = new MatchMessages()
        this._controller.on('publish', (message) => {
            const matchedMessage = this._matchMessages.matchAndUpdateReplyMessage(message)
            matchedMessage.qos = this._options.qos
            matchedMessage.retain = this._options.retain
            this._publish(matchedMessage)
        })
    }

    /**
     * 
     * @param {Object} config configuration settings for zwave
     */
    setDeviceConfiguration(config) {
        this._controller.setDeviceConfiguration(config)
        this._publish(new Message('$SYS/zwave/info', 'configuration reloaded', 'updated'))
    }

    /**
     * Iterates through the zwave devices adding value ids
     * @private
     */
    _setValueIdToDevices () {
        const devices = new Devices(this._options)
        devices.iterate('')
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
     * Publishes a command based on a service definition
     * @param {string} deviceId identifier of the device
     * @param {string|number} value value to publish
     * @param {Service} service service definition
     * @private
     */
    _publish (message) {
        this._callbacks.invokeCallback('publish', message)
    }

    /**
     * Closes the broker, stops listening
     */
    async close () {
        this._controller.close()
    }

    /**
     * Processes an incoming mqtt message
     * @param {Message} message mqtt message
     */
    handleMessage (message) {
        try {
            const { topic, value, reason } = message
            if (topic === '$SYS/zwave/removefailednode/set') {
                this._controller.removeFailedNode(value)
            } else if (topic === '$SYS/zwave/addnode/set') {
                this._controller.addDevice()
            } else if (topic === '$SYS/zwave/scan/set') {
                this._controller.startScan()
            } else {
                const mqttMessage = new Message(topic, value, reason)
                mqttMessage.addReason('received by zwave service')
                this._matchMessages.addReceivedMessage(mqttMessage)
                this._controller.setValue(topic, value)
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
        const subscriptions = {
            '$SYS/zwave/removefailednode/set': 2,
            '$SYS/zwave/addnode/set': 2,
        }
        for (const device of this._options.devices) {
            if (device.class_id === undefined) {
                const topic = device.topic + '/+/set'
                subscriptions[topic] = this._options.subscribeQoS
            } else {
                const topic = device.topic + '/set'
                subscriptions[topic] = this._options.subscribeQoS
            }
        }
        return subscriptions
    }

    /**
     * Called once after registration to handle additional tasks
     */
    run () {
        this._publish(new Message('$SYS/zwave/removefailednode', 'nop', 'zwave restarted'))
        this._publish(new Message('$SYS/zwave/addnode', 'nop', 'zwave restarted'))
        this._controller.requestConfigParametersForAllNodes()
    }
}

module.exports = ZwaveService




