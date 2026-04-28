/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Creates Mqtt message handler, receives, checks, transforms and foreward messages
 * It supports the callbacks 'send' and 'publish'
 */

'use strict'

const Message = require('@mangar2/message')
const LogFilter = require('@mangar2/logfilter')
const Connections = require('@mangar2/connections')
const { types, errorLog } = require('@mangar2/utils')
const mqttVersion = require('@mangar2/mqttversion')
const Callbacks = require('@mangar2/callbacks')

// set to true to print stack traces for debugging
const DEBUG = false

const {
    checkConnectPayload,
    checkDisconnectPayload,
    checkSubscribePayload,
    checkUnsubscribePayload
} = require('./checkdefinitions')

/**
 * @private
 * @description
 * Gets the quality of service from headers
 * @param {Object} headers headers with qos field
 * @returns {number} qos
 */
function getQos (headers) {
    const qos = Number(headers.qos)
    return qos === 1 ? 1 : (qos === 2 ? 2 : 0)
}

/**
 * @private
 * @description
 * Gets the dup flag from headers, convert to boolean
 * @param {Object} headers headers with dup flag
 * @returns {boolean} true, if dup is 'true' or '1'
 */
function getDup (headers) {
    return (headers.dup === '1' || headers.dup === 'true')
}

/**
 * @private
 * @description
 * Gets the retain flag from headers, convert to boolean
 * @param {Object} headers headers with retain flag
 * @returns {boolean} true, if retain is 'true' or '1'
 */
function getRetain (headers) {
    return (headers.retain === '1' || headers.retain === 'true')
}

/**
 * @callback sendCallback
 * @param {string} host host name
 * @param {number|string} port number
 * @param {string} path http path for sending data
 * @param {Object} payload payload (body) to send
 * @param {Object} headers headers to send
 */

/**
 * @callback publishCallback
 * @param {Message} message message published
 */

/**
 * Object containing all information to create a http return information
 * @typedef {Object} httpReturn
 * @property {Object} headers header parameters
 * @property {Object} payload payload data
 * @property {number} statusCode
 */

/**
 * Creates Mqtt message handler. It supports the callbacks 'send' and 'publish'
 * @param {Object} configuration configuration options for the instance
 * @param {Object} configuration.log logging options
 * @param {string} configuration.fileName filename for persistence
 * @param {string} configuration.directory directory (file path) for persistence
 * @param {number} configuration.replyTimeoutInMilliseconds timeout to wait for an answer
 * of a http call, default 10 seconds
 * @param {number} configuration.inFlightWindow amount of qos 1 and qos 2 messages
 * send for the same topic at the same time, default 1
 * @param {number} configuration.pubrelTimeoutInMilliseconds timout to wait for a
 * corresponding pubrel message, default one day
 * @param {number} configuration.maxRetryCount amount of resend of messages
 * before forcing a disconnect
 * @param {number} configuration.maxQueuSize maximal amount of entries in a message queue
 */
class Mqtt {
    constructor (configuration) {
        this.connections = new Connections(configuration)
        this.logFilter = new LogFilter()
        if (configuration.log !== undefined) {
            this.logFilter.changePattern(configuration.log)
        }

        this._callbacks = new Callbacks(['login', 'authenticate', 'send', 'publish'])
        this.on('login', (username, password) => { return true })
        this.on('authenticate', (token) => { return true })
    }

    /**
     * Restores the broker state from a file
     */
    restoreFromFile () {
        try {
            this.connections.restoreFromFile()
        } catch (err) {
            errorLog(err, DEBUG)
        }
    }

    /**
     * Sets a callback.
     * @param {'send'|'publish'} event callback name
     * @param {sendCallback|publishCallback} callback the callback for send and publish
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) { this._callbacks.on(event, callback) }

    /**
     * @private
     * @description
     * Sets the topics to log. Uses the mqtt topic match strings with wildchards # and +
     * @param {Object} payload {topic: type, topic: type, ... } type = "received, send, all"
     * @returns {Object} {Content-Type, body, statusCode}
     */
    setLog (payload) {
        this.logFilter.changePattern(payload)
        const result = {
            headers: { 'Content-Type': 'application/json' },
            payload: JSON.stringify(this.logFilter.moduleFilter),
            statusCode: 200
        }
        return result
    }

    /**
     * @private
     * @description
     * Internal use only: publishes a message
     * @param {Object} message message to publish
     * @param {number} qos quality of service
     * @param {boolean} dup duplicate flag
     * @param {boolean} retain retain flag
     */
    publish (message, qos, dup, retain) {
        if (this.logFilter.getLogLevel('receive', message.topic) !== undefined) {
            const reason = message.reason === undefined ? '' : message.reason[0].message
            const dupStr = dup ? ',dup' : ''
            const retainStr = retain ? ',retain' : ''
            const valueStr = message.value === '' || message.value === undefined ? '' : '=' + message.value
            console.log('%s (qos%s%s%s) received %s%s [%s]', new Date().toLocaleString(), qos, dupStr, retainStr, message.topic, valueStr, reason)
        }
        this._callbacks.invokeCallback('publish', message)
        this.connections.publishMessage(message, qos, retain)
    }

    /**
     * Publishes a log - message
     * @param {string} service name of the service bringing the message
     * @param {string} value value of the log message
     * @param {string} clientId name of the client involved
     */
    _publishLogMessage (service, value, reason, clientId) {
        const topic = '$SYS/broker/' + service + '/' + clientId
        const IS_NOT_DUPLICATE = false
        const DO_NOT_RETAIN = false
        reason = reason + ' (' + clientId + ')'
        const message = new Message(topic, value, reason)
        this.publish(message, 1, IS_NOT_DUPLICATE, DO_NOT_RETAIN)
    }

    /**
     * @private
     * @description
     * Receives a pubrel packages
     * @param {Object} payload publish payload
     * @param {Object} headers headers parameter {packetid}
     * @returns {Object} {headers, payload, statusCode}
     */
    onPubrel (payload, headers) {
        const result = mqttVersion.onPubrel(headers)
        this.connections.onPubrel(payload.token, result.packetid)
        return result
    }

    /**
     * @private
     * @description
     * Receives a message to pubish
     * @param {Object} payload publish payload
     * @param {Object} headers header parameters {qos, dup, packetid, retain}
     * @param {number} headers.qos quality of service
     * @param {(number|string)} [headers.dup] duplicate flag
     * @param {number} headers.packetid id of the packet
     * @param {(number|boolean)} [headers.retain] retain flag (true or 1 to retain the message)
     * @param {string} [headers.version='0.0'] interface version
     * @returns {httpReturn} http return information
     */
    onPublish (payload, headers) {
        const token = payload.token
        const message = payload.message === undefined ? payload : payload.message
        const messageObj = new Message(message.topic, message.value, message.reason)
        let isDuplicate = false
        messageObj.addReason('received by broker')

        if (headers.version !== undefined && headers.version !== '0.0' && token !== undefined) {
            const clientId = this.connections.checkToken(token)
            this.connections.getClientById(clientId).actualizeLastActiveTimestamp()
            isDuplicate = this.connections.isDuplicateQos2message(clientId, message.topic,
                getQos(headers), getDup(headers), headers.packetid)
        }

        const result = mqttVersion.onPublish(headers)

        if (!isDuplicate) {
            this.publish(messageObj, getQos(headers), getDup(headers), getRetain(headers), result.packetid)
        }

        return result
    }

    /**
     * @private
     * @description
     * Connects a client to the broker, storing the connection informations
     * @param {Object} payload payload {clientId, host, port, clean, will}
     * @param {string} payload.clientId id of the connecting client
     * @param {string} payload.host host name (or ip) of the client
     * @param {number} payload.port port number
     * @param {boolean} payload.clean true, if the session shall be cleaned
     * @param {number} payload.keepAlive timeout in milliseconds for disconnecting
     * if no message is transmitted
     * @param {string} payload.password connection password
     * @param {string} payload.user connection user name
     * @param {Object} payload.will message to be send on connection loss
     * @param {Object} headers received headers
     * @param {string} headers.version interface version
     * @returns {httpReturn} http return information
     */
    onConnect (payload, headers) {
        if (types.isString(payload.port)) {
            payload.port = parseInt(payload.port)
        }
        checkConnectPayload.throwOnValidationError(
            payload, 'Connect wrong parameters (' + payload.clientId + '): ')
        const { clientId, host, port, clean, password, user, will } = payload
        let { keepAlive } = payload
        const { version = '0.0' } = headers
        if (keepAlive === undefined && version === '0.0') {
            keepAlive = 0
        }
        if (!this._callbacks.invokeCallback('login', user, password)) {
            throw Error('Could not login')
        }
        const connect = this.connections.connect(clientId, host, port, clean, version, keepAlive)
        if (connect.mqttcode !== 0) {
            throw Error('Could not connect (code: ' + connect.mqttcode + ')')
        }
        const result = mqttVersion.onConnect(headers, { present: connect.present, token: connect.token })
        this.connections.setWill(clientId, will)
        this._publishLogMessage('connect', 'success', 'client request', payload.clientId)
        return result
    }

    /**
     * @private
     * @description
     * disconnects a client from the broker
     * @param {Object} payload
     * @param {string} payload.clientId Id of the client to disconnect
     * @param {Object} headers received headers
     * @param {string} headers.version interface version
     * @returns {httpReturn} http return information
     */
    onDisconnect (payload, headers) {
        checkDisconnectPayload.throwOnValidationError(
            payload, 'Disconnect wrong parameters (' + payload.clientId + '): ')
        this.connections.disconnect(payload.clientId)
        this._publishLogMessage('disconnect', 'success', 'client request', payload.clientId)
        const result = mqttVersion.onDisconnect(headers)

        return result
    }

    /**
     * @private
     * @description
     * Subscribes to topics
     * @param {Object} payload {clientId, topics}
     * @param {Object} headers received headers
     * @param {number} headers.packetid id of the packet
     * @param {string} headers.version interface version
     * @returns {httpReturn} http return information
     */
    onSubscribe (payload, headers) {
        checkSubscribePayload.throwOnValidationError(
            payload, 'Subscribe wrong parameters (' + payload.clientId + '): ')

        const qos = this.connections.subscribe(
            payload.clientId,
            payload.subscribe === undefined ? payload.topics : payload.subscribe
        )
        this._publishLogMessage('subscribe', 'success', 'client request', payload.clientId)

        const result = mqttVersion.onSubscribe(headers, qos)
        return result
    }

    /**
     * @private
     * @description
     * Unsubscribes to topics
     * @param {Object} payload {clientId, topics}
     * @param {string} payload.clientId id of the connecting client
     * @param {Object} payload.topics topics to subscribe to
     * @param {Object} headers received headers
     * @param {string} headers.version interface version
     * @returns {httpReturn} http return information
     */
    onUnsubscribe (payload, headers) {
        checkUnsubscribePayload.throwOnValidationError(
            payload, 'Unsubscribe wrong parameters (' + payload.clientId + '): ')

        this._publishLogMessage('unsubscribe', 'success', 'client request', payload.clientId)

        this.connections.unsubscribe(payload.clientId, payload.topics)
        const result = mqttVersion.onUnsubscribe(headers)

        return result
    }

    /**
     * @private
     * @description
     * Answers to a ping
     * @param {Object} payload http call payload
     * @param {string} payload.token
     * @returns {httpReturn} http return information
     */
    onPingreq (payload) {
        const client = this.connections.getClientByToken(payload.token)
        if (client === undefined) {
            throw Error('pingreq with illegal token')
        }

        client.actualizeLastActiveTimestamp()
        this._publishLogMessage('ping', 'success', 'client request', client.clientId)

        const result = {
            headers: { 'Content-Type': 'application/json', packet: 'pingresp' },
            payload: '',
            statusCode: client.isConnected() ? 204 : 400
        }
        return result
    }

    /**
     * @description
     * Calls a connection interface
     * @param {string} controls name of the mqtt controls to call (connect, disconnect, ...)
     * @param {Object} payload parameters for the interface (see interfaces)
     * @param {Object} headers headers of the http message
     * @returns {httpReturn} http return information
     */
    processRequest (controls, payload, headers) {
        let result = { headers: { 'content-type': 'text/plain' }, payload: 'request path not found: ' + controls, statusCode: 404 }
        try {
            switch (controls) {
            case 'publish': result = this.onPublish(payload, headers); break
            case 'pubrel': result = this.onPubrel(payload, headers); break
            case 'connect': result = this.onConnect(payload, headers); break
            case 'disconnect': result = this.onDisconnect(payload, headers); break
            case 'subscribe': result = this.onSubscribe(payload, headers); break
            case 'unsubscribe': result = this.onUnsubscribe(payload, headers); break
            case 'pingreq': result = this.onPingreq(payload); break
            case 'log': result = this.setLog(payload); break
            }
        } catch (err) {
            let error = (typeof (err) === 'string') ? err : err.message
            error = controls + ' ' + error
            this._publishLogMessage(controls, 'fail', 'client request', payload.clientId)
            errorLog(err, DEBUG)
            result = {
                headers: { 'content-type': 'application/json' },
                payload: JSON.stringify({ error }),
                statusCode: 400
            }
        }
        return result
    }

    /**
     * @description
     * Gets all connections or a single connection (depends on path)
     * @param {string} clientId id of the client or ""
     * @returns {httpReturn} http return information
     */
    getConnections (clientId) {
        const result = {
            headers: { 'Content-Type': 'application/json' },
            payload: {},
            statusCode: 200
        }
        if (clientId === '') {
            result.payload = this.connections.clients
        } else {
            try {
                result.payload = this.connections.getClientById(clientId, false)
            } catch (err) {
                result.payload = err.message
            }
        }
        return result
    }

    /**
     * @private
     * @description
     * Sends a publish message by invoking a callback
     * @param {MessageQueueEntry} options options for the callback
     * @param {string} options.host host name
     * @param {number} options.port port number
     * @param {string} options.version version string
     * @param {string} options.token send token
     * @param {Message} options.payload payload to send
     * @param {number} options.qos quality of service
     * @param {number} options.dup 1, if the packet is a duplicate (else 0)
     * @param {number} options.packetid packet id
     * @returns {boolean} true, if sending was successful
     */
    async _sendPublish (options) {
        const { host, port, version, token, payload: message, qos, dup, packetid } = options
        const dupStr = dup ? ',dup' : ''
        const valueStr = message.value === '' || message.value === undefined ? '' : '=' + message.value
        const retain = false
        const publishData = mqttVersion.publish(version, token, message, qos, dup, retain, packetid)
        let success = false

        try {
            const result = await this._callbacks.invokeCallbackAsync(
                'send', host, port, 'publish', publishData.payload, publishData.headers)

            success = publishData.resultCheck(result)
            if (success) {
                this.connections.acknowledgeMessage(options)
            }
            if (this.logFilter.getLogLevel('send', message.topic) !== undefined) {
                const reason = message.reason === undefined ? '' : message.reason[0].message
                console.log('%s %s:%s (qos%s%s) send %s=%s [%s]', new Date().toLocaleString(), host, port, qos, dupStr, message.topic, valueStr, reason)
            }
        } catch (err) {
            if (this.logFilter.getLogLevel('send', '$SYS/broker/error') !== undefined) {
                console.log('%s %s:%s send %s=%s [%s]', new Date().toLocaleString(), host, port, message.topic, valueStr, err.message)
            }
        }
        return success
    }

    /**
     * @private
     * @description
     * Sends a pubrel message by invoking a callback
     * @param {MessageQueueEntry} options options for the callback
     * @param {string} options.host host name
     * @param {number} options.port port number
     * @param {string} options.version version string
     * @param {string} options.token send token
     * @param {number} options.packetid packet id
     * @returns {boolean} true, if sending was successful
     */
    async _sendPubrel (options) {
        const { host, port, version, token, packetid } = options
        const publishData = mqttVersion.pubrel(version, token, packetid)
        let success = false

        try {
            const result = await this._callbacks.invokeCallbackAsync(
                'send', host, port, 'pubrel', publishData.payload, publishData.headers)

            success = publishData.resultCheck(result)
            if (success) {
                this.connections.acknowledgeMessage(options)
            }
        } catch (err) {
            if (this.logFilter.getLogLevel('send', '$SYS/broker/error') !== undefined) {
                console.log('%s %s:%s pubrel [%s]', new Date().toLocaleString(), host, port, err.message)
            }
        }
    }

    /**
     * Cleans up connections on timeout
     * @private
     */
    _cleanup () {
        const disconnected = this.connections.cleanup()
        for (const clientId in disconnected) {
            const info = disconnected[clientId]
            this._publishLogMessage(
                'disconnect', 'cleanup', info.reason + ' (' + info.keepAlive + ')', clientId)
        }
    }

    /**
     * @description
     * Processes all messages to send and for each message calls the provided callback functions registered by calls to 'on':
     * 'send' (host, port, action, payload, headers)
     * @returns {number} amount of messages sent
     */
    async processSendMessage () {
        const messages = this.connections.getAllMessagesToSend()
        this._cleanup()
        let promises = []
        for (const message of messages) {
            let result
            if (message.isStatusPubrel()) {
                result = this._sendPubrel(message)
            } else {
                result = this._sendPublish(message)
            }
            promises.push(result)
            if (promises.length >= 10) {
                Promise.all(promises)
                promises = []
            }
        }
        Promise.all(promises)
        return messages.length
    }
}

module.exports = Mqtt
