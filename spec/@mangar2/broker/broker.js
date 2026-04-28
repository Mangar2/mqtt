/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview The (mqtt) broker is a central service receiving and distributing messages to all
 * subscribed receipiens. Use the "brokerservice" module to start the broker.
 */

'use strict'

const { shutdown } = require('@mangar2/utils')
const Server = require('@mangar2/httpservice').HttpServer
const Client = require('@mangar2/httpservice').HttpClient
const Connect = require('@mangar2/connect')
const MqttInterface = require('@mangar2/mqtt')

/**
 * @private
 * @description
 * Gets the interface (first section of the path)
 * @param {string} path http path
 * @returns {string} first section of the path
 */
function _getInterface (path) {
    const pathArray = path.split('/').splice(1)
    const calledInterface = pathArray[0]
    return calledInterface
}

/**
 * @private
 * @description
 * Gets the client id of the path (the second section of the path)
 * @param {string} path http path
 * @returns {string} client id of the path (second part)
 */
function _getClientId (path) {
    const pathArray = path.split('/').splice(2)
    const clientId = pathArray.join('/')
    return clientId
}

/**
 * @private
 * @description
 * Pauses the execution for a while (needs to "wait") for the result.
 * @param {number} timeInMilliseconds delay in milliseconds
 * @returns {Promise} promise to wait for
 */
function _delay (timeInMilliseconds) {
    return new Promise(resolve => setTimeout(resolve, timeInMilliseconds))
}

/**
 * Creates a new broker
 * @param {Object} configuration configuration for the broker
 * @param {number|string} configuration.port port number the broker will listen on
 * @param {number} [configuration.persistInterval = 60 * 1000] Intervall between persisting
 * internal state to file in milliseconds. Persistance is not called if this value is 0
 * @param {Object} configuration.connections connection settings for the broker
 * @param {string} configuration.connections.fileName filename for persistence
 * @param {string} configuration.connections.directory directory (file path) for persistence
 * @param {number} configuration.connections.replyTimeoutInMilliseconds timeout to wait for an answer
 * of a http call
 * @param {number} [configuration.connections.inFlightWindow=1] amount of qos 1 and qos 2 messages
 * send for the same topic at the same time
 * @param {number} [configuration.connections.pubrelTimeoutInMilliseconds=one day] temeout to wait for a
 * corresponding pubrel message
 * @param {number} configuration.connections.maxRetryCount amount of resend of messages
 * before forcing a disconnect
 * @param {number} configuration.connections.maxQueuSize maximal amount of entries in a message queue
 * @param {Object} configuration.connections.log log settings
 * @example
 * const configuration = {
 *      port: 10000,
 *      persistInterval: 0,
 *      connections: {
 *           directory: '.',
 *           fileName: 'broker',
 *           log: [
 *               {
 *                   module: 'received',
 *                   topic: '#'
 *               }
 *           ]
 *       }
 *   }
 * const broker = new Broker(configuration)
 * broker.run()
 */
class Broker {
    constructor (configuration) {
        this.running = false
        this._configuration = configuration
        this.server = new Server(this._configuration.port)
        this.mqtt = new MqttInterface(this._configuration.connections)
        this.mqtt.restoreFromFile()
        this.changed = false

        this.server.on('put', (payload, headers, path, res) => {
            this.changed = true
            const command = _getInterface(path)
            const receivedPayload = JSON.parse(payload)
            const result = this.mqtt.processRequest(command, receivedPayload, headers)
            result.headers['Access-Control-Allow-Origin'] = this.getOrigin(headers)
            res.writeHead(result.statusCode, result.headers)
            res.end(result.payload)
        })

        this.server.on('get', (payload, headers, path, res) => {
            const command = _getInterface(path)
            if (command === 'clients') {
                const result = this._getClientInfo(path)
                const resultPayload = JSON.stringify(result.payload)
                result.headers['Access-Control-Allow-Origin'] = this.getOrigin(headers)
                res.writeHead(result.statusCode, result.headers)
                res.end(resultPayload)
            } else {
                throw Error('unknown path ' + path)
            }
        })

        this.server.on('options', (payload, headers, path, res) => {
            res.writeHead(200, { 
                'Access-Control-Allow-Methods': 'OPTIONS, GET, PUT', 
                'Access-Control-Allow-Origin': this.getOrigin(headers),
                'Access-Control-Allow-Headers': 'content-type'
            })
            res.end()
        })

        this.mqtt.on('send', async (host, port, path, payload, headers) => {
            const client = new Client(host, port)
            const result = await client.send('/' + path, 'PUT', payload, headers)
            return result
        })

        shutdown(async () => {
            await this.close()
            process.exit(0)
        })
    }

    /**
     * Gets the required origin based on the headers sent
     * @param {Object} headers headers of a call
     * @param {string} headers.origin origin of the call
     */
    getOrigin(headers) {
        const allowedOrigin = this._configuration.cors.allowOrigin
        const origin = headers.origin
        if (allowedOrigin.includes(origin) || allowedOrigin.includes('*')) {
            return origin
        } else {
            return ''
        }

    }

    /**
     * Connects to a friendly broker and subscribs for all messages
     * @param {string} host name of the host (or his ip)
     * @param {number} port port number to connect to
     */
    connect (host, port) {
        this.connect = new Connect('broker/new', host, port, this._configuration.port)
        this.connect.connectAndSubscribe(false, { '#': 1 }, '0.0')
    }

    /**
     * Sets a callback.
     * @param {string} event callback name (supports 'send' and 'publish')
     * 'send' is called to send data. This function is provided by the broker but could be overwritten
     * 'publish' is called on receiving data. This function if provided by the broker but could be overwritten
     * @param {function} callback
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) { this.mqtt.on(event, callback) }

    /**
     * Starts the broker. It will listen to message, send messages and periodically store its status
     */
    run () {
        this.running = true
        this.server.listen()
        if (this._configuration.persistInterval !== 0) {
            this._persistBroker()
        }
        this._processMessages()
    }

    /**
     * Closes the broker, stops listening
     */
    async close () {
        this.running = false
        await this.server.close()
        if (this._configuration.persistInterval !== 0) {
            await this.mqtt.connections.persist()
        }
    }

    /**
     * @private
     * @description
     * Send all messages that are "ready to send"
     */
    async _processMessages () {
        while (this.running) {
            let messagesSent = 0
            messagesSent = await this.mqtt.processSendMessage()
            await _delay(messagesSent > 0 ? 5 : 50)
        }
    }

    /**
     * @private
     * @description
     * Stores the broker connection data to file
     */
    async _persistBroker () {
        while (this.running) {
            if (this.changed) {
                this.changed = false
                await this.mqtt.connections.persist()
            }
            await _delay(this._configuration.persistInterval)
        }
    }

    /**
     * @private
     * @description
     * Gets information about the client status
     * @param {string} path http path to the client 'client/clientname
     */
    _getClientInfo (path) {
        const clientId = _getClientId(path)
        const result = this.mqtt.getConnections(clientId)
        return result
    }
}

module.exports = Broker
