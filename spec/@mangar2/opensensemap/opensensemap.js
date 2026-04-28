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

const HttpsClient = require('@mangar2/httpservice').HttpsClient
const Message = require('@mangar2/message')

/**
 * @description
 * Sensor information
 * @typedef {Object} Sensor
 * @property {string} name name of the sensor
 * @property {string} unit unit of the sensor
 * @property {string} topic topic the sensor is matched to
 * @property {string} id opensensmap unique id
 */

/**
 * List of Sensors
 * @typedef {Sensor[]} Sensors
 */

/**
 * Creates a class with the ability to publish information to the opensensemap service
 * @param {Object} config configuration (sensors, ...)
 * @param {string} [config.station] name of the station
 * @param {string} config.id sensebox id (provided by open sens map)
 * @param {string} config.host host of open sense map
 * @param {port} [config.port=443] portnumer of open sense map (usually 443)
 * @param {Sensors} config.sensors opensensemap sensor configuration
 * @returns {Message} message containing the status. Send it back to the broker
 * @example
 * const config =
 * {
 *   "station" : "your station name (optional)",
 *   "id" : "your station id",
 *   "host": "ingress.opensensemap.org",
 *   "port": 443,
 *   "sensors": [{
 *           "name": "temperature",
 *           "uint": "°C",
 *           "topic" : "topic/used/for/the/sensor",
 *           "id": "your sensor id"
 *    }]
 * }
 * const opensenseMap = new Opensensemap(config)
 *
 * let result = await opensensemap.publish(
 *    new Message('topic/used/for/the/sensor', 2.61, 'test value' }
 * )
*/
class OpensenseMap {
    constructor (config) {
        this._config = config
    }

    /**
     * @private
     * @description
     * Searches a sensor-id in the "sensors" configuration
     * @param {Sensors} sensors array of sensor objects with "topic" and "id" attributes
     * @param {string} topic topic to search for
     * @returns {string|undefined} opensensmap unique id of the sensor or undefined if not found
     */
    _lookupSensorId (sensors, topic) {
        let sensorId
        for (const sensor of sensors) {
            if (sensor.topic === topic) {
                sensorId = sensor.id
                break
            }
        }
        return sensorId
    }

    /**
     * Gets the subscriptions for the service
     * @returns {string[]} array of subscriptions
     */
    getSubscriptions () {
        const subscriptions = {}
        const { sensors, qos } = this._config
        for (const sensor of sensors) {
            subscriptions[sensor.topic] = qos
        }
        return subscriptions
    }

    /**
     * @private
     * @description
     * Gets the topic to return
     * @param {number} statusCode http status code
     * @param {Reason} messageReason reason of the status
     * @param {string} resultReason result information from opensensemap
     * @returns {Message} result message
     */
    _getReturnMessage (statusCode, messageReason, resultReason) {
        let result
        switch (statusCode) {
        case 201: result = new Message('$SYS/opensensemap/success', statusCode, messageReason); break
        case 404: result = new Message('$SYS/opensensemap/error', statusCode, messageReason); break
        case 422: result = new Message('$SYS/opensensemap/error', statusCode, messageReason); break
        default: result = new Message('$SYS/opensensemap/error', statusCode, messageReason); break
        }
        result.addReason(resultReason)
        result.qos = 1
        return result
    }

    /**
     * Publishes a value to opensensemap (you need an account to do this)
     * Publish data provided as Message to opensensemap. You need an account on opensensemap.org
     * The message topic is mapped to the sensor id provided in the config object. The message value it the value to publish
     * @param {Message} message the data to publish on the opensensemap
     * @param {string} message.topic the topic is used to select the sensor id from config
     * @param {number} message.value the value to publish to the sensor
     */
    async handleMessage (message) {
        const { sensors, id, host, port = 443 } = this._config
        const { topic, value, reason: messageReason } = message
        const httpsClient = new HttpsClient(host, port)
        const sensorId = this._lookupSensorId(sensors, topic)
        if (sensorId === undefined) {
            throw Error('topic ' + topic + ' not found in open map sensors configuraiton ')
        }
        const data = { value }
        const payload = JSON.stringify(data)
        const headers = { 'content-type': 'application/json; charset=UTF-8' }
        const path = '/boxes/' + id + '/' + sensorId

        const httpResult = await httpsClient.send(path, 'POST', payload, headers)
        let resultReason = payload
        if (headers['content-type'].startsWith('application/json')) {
            const jsonPayload = JSON.parse(httpResult.payload)
            resultReason = jsonPayload.message !== undefined ? jsonPayload.message : jsonPayload
            resultReason = topic + '(' + value + '): ' + resultReason
        }

        const returnMessage = this._getReturnMessage(httpResult.statusCode, messageReason, resultReason)
        return returnMessage
    }
}

module.exports = OpensenseMap
