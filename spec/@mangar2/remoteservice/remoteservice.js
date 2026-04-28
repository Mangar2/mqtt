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
const { HttpServer } = require('@mangar2/httpservice')
const Message = require('@mangar2/message')
const sanitizeConfiguration = require('./configuration')

/**
 * Standard header for JSON replies
 * @private
 */
const standardHeaderText = {
    'content-type': 'text/plain; charset=UTF-8',
    accept: 'application/json,text/plain',
    'accept-charset': 'UTF-8'
}

/**
 * Service configuration
 * @typedef {Object} Service
 * @property {string} path path of the service
 * @property {string} reason service usage information used as reason string
 * @property {number} [qos = 1] quality of service
 * @property {Object} devices key: topic list
 */

/**
 * Creates a remote service
 * @param {Object} [options={}] remote server parameter
 * @param {Service[]} options.services list of supported services
 */
class RemoteService {
    constructor (options = {}) {
        this._options = sanitizeConfiguration(options)
        this._httpServer = new HttpServer(options.server.port)
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
     * Searches for the service with the given path
     * @param {string} path current path information
     * @returns {Service|undefined} found service or undefined, if no services is matching
     * @private
     */
    _getMatchingService (path) {
        let result
        for (const service of this._options.services) {
            if (service.path === path) {
                result = service
                break
            }
        }
        return result
    }

    /**
     * Publishes a command based on a service definition
     * @param {string} deviceId identifier of the device
     * @param {string|number} value value to publish
     * @param {Service} service service definition
     * @private
     */
    _publish (deviceId, value, deviceToken, service) {
        const { reason = 'remote command', qos = 1, devices } = service
        if (deviceId === undefined) {
            throw Error('DeviceId is undefined')
        }
        if (value === undefined) {
            throw Error('Value is undefined')
        }
        const topic = devices[deviceId]
        if (topic === undefined) {
            throw Error('Unknown device id %s with token %s', deviceId, deviceToken)
        }
        const message = new Message(topic, value, reason)
        message.qos = qos
        this._callbacks.invokeCallback('publish', message)
    }

    /**
     * Sets the httpServer callback
     * @param {string} method http method (POST, PUT, GET, ...)
     * @private
     */
    _setCallback (method) {
        const callback = (payload, headers, path, res) => {
            try {
                const service = this._getMatchingService(path)
                if (service === undefined) {
                    throw Error('unknown service path ' + path)
                }
                if (method === 'POST') {
                    const { deviceId, state: value, deviceToken } = JSON.parse(payload)
                    this._publish(deviceId, value, deviceToken, service)
                } else if (method === 'GET') {
                    this._publish(payload.get('deviceId'), payload.get('state'), payload.get('accessToken'), service)
                }
                res.writeHead(200, standardHeaderText)
                res.end('ok')
            } catch (err) {
                errorLog(err)
                res.writeHead(404, standardHeaderText)
                res.end('Service not found')
            }
        }
        this._httpServer.on(method, callback)
    }

    /**
     * Listens to a configured port
     */
    run () {
        this._setCallback('GET')
        this._setCallback('POST')
        this._httpServer.listen()
    }

    /**
     * Closes the broker, stops listening
     */
    async close () {
        await this._httpServer.close()
    }
}

module.exports = RemoteService
