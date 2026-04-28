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

const Devices = require('@mangar2/devices')
const { types } = require('@mangar2/utils')

/**
 * @typedef {Object} ZwaveValue
 * @property {number} node_id id of the zwave node
 * @property {number} class_id id of the zwave parameter class
 * @property {string} label label of the value changed
 * @property {number} [instance = 1] index of device instance for a multi instance zwave device
 * @property {number} [index = 0] index of the feature for a multi-feature device class
 */

/**
 * @typedef {Object} DeviceConfig
 * @property {string} topic device topic
 * @property {number} node_id id of the zwave node
 * @property {number} [class_id] id of the zwave parameter class
 * @property {number} [instance] index of device instance for a multi instance zwave device
 * @property {number} [index] index of the feature for a multi-feature device class
 * @property {string} [type = 'bool'] type of the parameter (e.g. 'bool')
 */

/**
 * Constructs a new zwave controller and binds it to an input device
 * @param {object} config zwave controller configuration
 * @param {object|array} config.devices device definition structure
 */
class ZwaveDevices {
    constructor (config) {
        this._devices = new Devices(config)
        this._valueIdToTopicCache = {}
    }

    /**
     * Recursively searches for an object in the zwave node tree
     * @param {object} object object to search for the right zwave object
     * @param {string} label label of the zwave object to search for
     * @param {number} instance instance number of the zwave object to search for
     * @private
     */
    _getObjectByLabelRec (object, label, instance) {
        let result
        if (typeof (object) === 'object') {
            if (object.class_id) {
                if (object.label === label && object.instance === instance) {
                    result = object
                }
            } else {
                for (const element in object) {
                    const subObject = object[element]
                    result = this._getObjectByLabelRec(subObject, label, instance)
                    if (result) {
                        break
                    }
                }
            }
        }
        return result
    }

    /**
     * Gets a zwave controllable element by label
     * @param {object[]} nodes zwave nodes structure
     * @param {string} label label of the setting
     * @param {number} nodeId id of the node
     * @param {number} [instanceId=1] id of the instance
     * @private
     */
    _getObjectByLabel (nodes, label, nodeId, instanceId = 1) {
        const classes = nodes[nodeId].classes
        const result = this._getObjectByLabelRec(classes, label, instanceId)
        return result
    }

    /**
     * Calculates the matching of a device and a value
     * @param {DeviceConfig} deviceConfig new value information structure of the device class
     * @param {ZwaveValue} zwaveValue new value information structure of the device class
     * @returns {number} number of matching sub-items
     * @private
     */
    _calcMatch(deviceConfig, zwaveValue) {
        const { class_id, instance, index } = zwaveValue
        const classIdMatches = (deviceConfig.class_id === undefined) || (deviceConfig.class_id === class_id)
        const instanceMatches = (deviceConfig.instance === undefined) || (deviceConfig.instance === instance)
        const indexMatches = (deviceConfig.index === undefined) || (deviceConfig.index === index)
        let result = 0
        if (classIdMatches && instanceMatches && indexMatches) {
            result = 1
            result += deviceConfig.class_id === class_id
            result += (deviceConfig.index === index) * 2
            result += (deviceConfig.instance === instance) * 4
        }
        return result
    }

    /**
     * Calculates the best match for a zwave value
     * @param {ZwaveValue} zwaveValue new value information structure of the device class
     * @returns {{topic:string, type:string}} topic, if found and device type. Both can be undefined
     */
    _findBestMatch(zwaveValue) {
        let result = { }
        const { label, node_id } = zwaveValue
        const devices = this._devices.attributeToDevices('node_id', node_id)
        if (!types.isArray(devices)) {
            throw('error: array of devices expected ')
        }
        let match = 0
        for (const deviceConfig of devices) {
            const newMatch = this._calcMatch(deviceConfig, zwaveValue)
            if (newMatch > match) {
                result.topic = deviceConfig.topic
                result.type = deviceConfig.type
                if (deviceConfig.class_id === undefined && label !== undefined) {
                    // device configurations without class_id are valid for several class_id´s with different labels
                    // Thus the topic does not include the class_id label
                    result.topic += '/' + label
                }
            }
        }
        return result
    }

    /**
     * Gets the right topic for a value object
     * @param {ZwaveValue} zwaveValue new value information structure of the device class
     * @returns {{topic:string, type:string}} topic, if found and device type. Both can be undefined
     */
    valueToTopicAndType (zwaveValue) {
        let result = { }
        const { value_id } = zwaveValue
        if (value_id !== undefined && this._valueIdToTopicCache[value_id] !== undefined) {
            result = this._valueIdToTopicCache[value_id]
        } else {
            result = this._findBestMatch(zwaveValue)
            if (value_id !== undefined) {
                this._valueIdToTopicCache[value_id] = result
            }
        }
        return result
    }


    /**
     * Gets a full set of zwave identifiers based on the zwave device configuration,
     * default values for instance, index and type - if they are undefined in the configuration 
     * and the class_id based on the label - if it is undefined in the configuration
     * @param {object[]} nodes zwave nodes structure
     * @param {string} label label of the setting
     * @param {DeviceConfig} device device configuration
     * @returns {DeviceConfig} device configuration filled with default values and label
     * @private
     */
    _completeDeviceConfig(nodes, label, device) {
        const { topic, node_id } = device
        let { class_id, instance, index, type } = device
        if (node_id === undefined) {
            throw Error('device is not a zwave node (missing node_id property in configuration) ' + topic)
        }
        if (instance === undefined) {
            instance = 1
        }
        if (index === undefined) {
            index = 0
        }
        if (type === undefined) {
            type = 'bool'
        }
        if (class_id === undefined && label === undefined) {
            throw Error('Neither label nor class_id specified, (configuration error) topic: ' + topic)
        }
        if (class_id === undefined) {
            const zwaveObject = this._getObjectByLabel(nodes, label, node_id, instance, index)
            console.log(zwaveObject)
            class_id = zwaveObject.class_id
            index = zwaveObject.index
            type = zwaveObject.type
        }
        return { node_id, class_id, instance, index, type }
    }

    /**
     * Gets a full set of zwave identifiers based on the zwave device configuration,
     * default values for instance, index and type - if they are undefined in the configuration 
     * and the class_id based on the label - if it is undefined in the configuration
     * @param {object[]} nodes zwave nodes structure
     * @param {topic} topic topic to look for
     * @param {string} label label of the setting
     * @returns {DeviceConfig} zwaveId (node_id, class_id, instance, index, type)
     */
    topicToZwaveId(nodes, topic, label) {
        let device = this._devices.topicToDevice(topic + '/' + label)
        if (device === undefined) {
            device = this._devices.topicToDevice(topic)
        }
        return this._completeDeviceConfig(nodes, label, device)
    }

}

module.exports = ZwaveDevices