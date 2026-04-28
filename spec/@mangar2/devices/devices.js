/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Access device configuration of a json data structure used for a device config file
 */

'use strict'

const { types } = require('@mangar2/utils')

/**
  * @private
  * @description
  * Gets a object of devices by "attributeName"
  * @param {object} devices list of devices
  * @param {string} attributeName name of the attribute to use as index
  * @param {string} link link to current device node
  * @returns {{object[]}} devices indexed by the attribute "attributeName"
  */
function getDevicesByAttribute (devices, attributeName, link) {
    var results = {}
    if (typeof devices === 'object') {
        for (const key in devices) {
            const device = devices[key]
            const curAttribute = device[attributeName]
            if (curAttribute !== undefined) {
                device.link = link
                if (results[curAttribute] === undefined) {
                    results[curAttribute] = [ device ]
                } else {
                    results[curAttribute] = [ ...results[curAttribute], device ]
                }
            }
        }
    }
    return results
}

/**
 * @private
 * @description
 * processes all devices of a tree recursively
 * @param {object} curNode current device node
 * @param {string} attributeName name of the attribute to use as index
 * @param {string} link link to current device node
 * @returns {{object[]}} devices indexed by the attribute "attributeName"
 */
function getDevicesRec (curNode, attributeName, link) {
    var resultRec = {}
    if (typeof curNode === 'object') {
        for (const key in curNode) {
            if (key === 'devices') {
                const devices = getDevicesByAttribute(curNode.devices, attributeName, link)
                resultRec = { ...resultRec, ...devices }
            } else {
                const childNode = curNode[key]
                const childLink = link + '/' + key
                const devices = getDevicesRec(childNode, attributeName, childLink)
                resultRec = { ...resultRec, ...devices }
            }
        }
    }
    return resultRec
}
/**
 * Constructs a new device and sets the device configuration
 * @param {object} config device configuration structure
 * @example
 * const deviceJson = {
 *  level0: {
 *       room1: {
 *           devices: {
 *               device1: {
 *                   'Friendly name': 'Washing machine',
 *                   topic: 'cellar/washing machine',
 *                   id: 'Washingmachine',
 *                   interface: 'zwave'
 *               }
 *           }
 *       }
 *   }
 * const devices = new Devices(deviceJson)
 * const deviceByTopic = devices('cellar/washing machine')
 * const deviceByAttribute = devices('id', 'Washingmachine')
 */
class Devices {
    constructor (config) {
        this._config = config
        this._cache = {}
    }

    /**
     * @callback iterateCallback
     * @param {string} element attribute value
     * @param {object} device device data
     */

    /**
     * private
     * Iterates through all configured devices
     * @param {string} attributeName name of the attribute to look for
     * @param {iterateCallback} callback function called for each topic
     */
    iterate (attributeName, callback) {
        if (this._cache[attributeName] === undefined) {
            this._cache[attributeName] = getDevicesRec(this._config, attributeName, '')
        }
        for (const element in this._cache[attributeName]) {
            const device = this._cache[attributeName][element]
            callback(element, device)
        }
    }

    /**
     * Maps a topic to a device
     * @param {string} topic topic to search for
     * @returns {object} device info object
     */
    topicToDevice (topic) {
        let result
        const devices = this.attributeToDevices('topic', topic)
        if (types.isArray(devices)) {
            result = devices[0]
        }
        return result
    }

    /**
     * Maps an attribute to a device
     * @param {string} attributeName name of the attribute to look foor
     * @param {string} attributeValue value of the attribute
     * @returns {object[]|undefined} array of devices with the same value for attributeName
     */
    attributeToDevices (attributeName, attributeValue) {
        if (this._cache[attributeName] === undefined) {
            this._cache[attributeName] = getDevicesRec(this._config, attributeName, '')
        }
        const map = this._cache[attributeName]
        if (Object.entries(map).length === 0) {
            throw Error('unknwon attribute name ' + attributeName)
        }
        const result = map[attributeValue]
        return result
    }
}

module.exports = Devices
