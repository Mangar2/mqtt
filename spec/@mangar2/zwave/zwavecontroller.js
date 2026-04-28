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

var OpenZWave = require('openzwave-shared')
const Message = require('@mangar2/message')
const { errorLog } = require('@mangar2/utils')
const Callbacks = require('@mangar2/callbacks')
const ZwaveDevices = require('./zwavedevices.js')

const ZWAVE_CONFIGURATION = 0x70

const mapNotification = {
    0: 'message completed',
    1: 'timeout',
    2: 'nop',
    3: 'node awake',
    4: 'node sleep',
    5: 'node dead',
    6: 'node alive'
}

/**
 * Constructs a new zwave controller and binds it to an input device
 * @param {object} config zwave controller configuration
 * @param {string} config.device zwave controller device (COM port)
 * @param {string} config.topic zwave controller topic to send messages
 */
class ZwaveController {

    constructor (config) {
        // this test assumes no actual ZWave controller exists on the system
        // and is just a rudimentary check that the driver can initialise itself.
        this._zwave = new OpenZWave({
            ConsoleOutput: false,
            Logging: true,
            SaveLogLevel: 3,
            QueueLogLevel: 3,
            DumpTriggerLevel: 3,
            SaveConfiguration: true,
            DriverMaxAttempts: 3,
            PollInterval: 500,
            SuppressValueRefresh: false
        })
        this._nodes = []
        this._homeid = null
        this._config = config
        this._zwaveDevices = null
        this._callbacks = new Callbacks(['publish'])

        this.registerZwaveCallbacks()
        this._zwave.connect(this._config.device)
        console.log('connected to ' + this._config.device)
    }

    /**
     * Sets the zwave device configuration
     * @param {Devices} devices tree of device configuration information
     */
    setDeviceConfiguration(devices) {
        this._zwaveDevices = new ZwaveDevices(devices)
    }

    /**
     * Publishes a notification information
     * @param {string|number} node_id id of the node 
     * @param {number} notif notification number 
     * @private
     */
    _notification(node_id, notif) {
        try {
            let topic = this._zwaveDevices.valueToTopicAndType({ node_id }).topic
            if (topic === undefined) {
                topic = '/$SYS/zwave/unknown node ' + node_id
            }
            const message = new Message(topic, mapNotification[notif], 'zwave notification')
            this._callbacks.invokeCallback('publish', message)
        } catch (err) {
            const message = new Message('$SYS/zwave/error', mapNotification[notif], 'node: ' + node_id + ' ' + mapNotification[notif])
            this._callbacks.invokeCallback('publish', message)
            errorLog(err)
        }
    }

    /**
     * Register callbacks for zwave
     */
    registerZwaveCallbacks () {
        this._zwave.on('driver ready', (homeId) => {
            this._homeid = homeId
            const message = new Message('$SYS/zwave/notification', 'starting scan', 'scanning homeid=0x' + this._homeid.toString(16))
            this._callbacks.invokeCallback('publish', message)
        })

        this._zwave.on('driver failed', () => {
            const message = new Message('$SYS/zwave/error', 'driver failure', 'failed to start driver. Stopping module')
            this._callbacks.invokeCallback('publish', message)
        })

        this._zwave.on('scan complete', () => {
            const message = new Message('$SYS/zwave/notification', 'scan complete', 'zwave info')
            this._callbacks.invokeCallback('publish', message)
        })
        this._zwave.on('notification', (nodeId, notif) => this._notification(nodeId, notif)) 
        this._zwave.on('controller command', (r, s) => {
            const message = new Message('$SYS/zwave/notification', s, 'controller commmand feedback: r=' + r + ' s=' + s)
            this._callbacks.invokeCallback('publish', message)
        })
        this._zwave.on('node added', nodeId => this._addNode(nodeId))
        this._zwave.on('node ready', (nodeId, nodeInfo) => this.setNodeInfo(nodeId, nodeInfo))
        this._zwave.on('value added', (nodeId, comClass, value) => this._onValueAdded(nodeId, comClass, value))
        this._zwave.on('value removed', (nodeId, comClass, index) => this._onValueRemoved(nodeId, comClass, index))
        this._zwave.on('value changed', (nodeId, comClass, value) => this.publishValueChange(nodeId, comClass, value))
        this._zwave.on('value refreshed', (nodeid, commandclass, valueId) => {
            console.log('refreshed: node=%d, class=%d, valueId=%d', nodeid, commandclass, valueId)
        })
    }

    /**
     * Sets a callback.
     * @param {string} event event name (not case sensitive) for the callback
     * @param {function} callback(parameter)
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) {
        this._callbacks.on(event, callback)
    }

    /**
     * Adds a new zwave device node
     * @param {string} nodeId id of the zwave device node
     * @private
     */
    _addNode (nodeId) {
        this._nodes[nodeId] = {
            manufacturer: '',
            manufacturerid: '',
            product: '',
            producttype: '',
            productid: '',
            type: '',
            name: '',
            loc: '',
            classes: {},
            ready: false
        }
    }

    /**
     * Logs the infos of a zwave device node to console
     * @param {string} nodeId id of the zwave device node
     */
    logNode (nodeId) {
        const nodeInfo = this._nodes[nodeId]
        const manufacturer = nodeInfo.manufacturer ? nodeInfo.manufacturer : 'id=' + nodeInfo.manufacturerid
        const product = nodeInfo.product ? nodeInfo.product : 'product=' + nodeInfo.productid
        const productType = nodeInfo.producttype

        console.log('node%d: %s, %s, type=%s', nodeId, manufacturer, product, productType)
        console.log('node%d: name="%s", type="%s", location="%s"', nodeId, nodeInfo.name, nodeInfo.type, nodeInfo.loc)

        for (const comclass in nodeInfo.classes) {
            var values = nodeInfo.classes[comclass]
            console.log('node%d: class %d', nodeId, comclass)
            for (const idx in values) {
                console.log('node%d:   %s=%s', nodeId, values[idx].label, values[idx].value)
            }
        }
    }

    /**
     * Sets the node info
     * @param {string} nodeId id of the zwave device node
     * @param {object} nodeInfo additional information for the node
     */
    setNodeInfo (nodeId, nodeInfo) {
        const node = this._nodes[nodeId]
        node.manufacturer = nodeInfo.manufacturer
        node.manufacturerid = nodeInfo.manufacturerid
        node.product = nodeInfo.product
        node.producttype = nodeInfo.producttype
        node.productid = nodeInfo.productid
        node.type = nodeInfo.type
        node.name = nodeInfo.name
        node.loc = nodeInfo.loc
        node.ready = true

        for (const comClass in node.classes) {
            switch (comClass) {
            case 0x25: // COMMAND_CLASS_SWITCH_BINARY
            case 0x26: // COMMAND_CLASS_SWITCH_MULTILEVEL
                this._zwave.enablePoll(nodeId, comClass)
                break
            }
        }

        // this.logNode(nodeId)
    }

    /**
     * Publishes a value
     * @param {string} nodeId id of the zwave device
     * @param {object} zwaveValue new value information structure of the device class
     * @param {string} reasonString the reason string
     */
    publishValue (nodeId, zwaveValue, reasonString) {
        try {
            let topic
            let { value } = zwaveValue
            if (nodeId === 1) {
                topic = this._config.topic
            } else {
                const deviceInfo = this._zwaveDevices.valueToTopicAndType(zwaveValue)
                topic = deviceInfo.topic
                reasonString += ', Zwave value: ' + value
                if (deviceInfo.type === 'switch') {
                    value = value ? 'on' : 'off'
                }
            }
            if (topic !== undefined) {
                const message = new Message(topic, value, reasonString)
                this._callbacks.invokeCallback('publish', message)
            }
        } catch (err) {
            console.log('Missing configuration entry for nodeId %s, value %s, reason %s', nodeId, zwaveValue, reasonString)
            const topic = '$SYS/zwave/' + nodeId
            const message = new Message(topic, zwaveValue.value, reasonString)
            this._callbacks.invokeCallback('publish', message)
            console.log(err)
        }
    }

    /**
     * Publishes a value change
     * @param {string} nodeId id of the zwave device
     * @param {string} comClass parameter class
     * @param {object} value new value information structure of the device class
     */
    publishValueChange (nodeId, comClass, value) {
        const node = this._nodes[nodeId]

        if (!node.classes[comClass]) {
            node.classes[comClass] = {}
        }
        node.classes[comClass][value.index] = value
        this.publishValue(nodeId, value, 'received from zwave, id: ' + value.value_id)
    }

    /**
     * Adds a value to the node tree
     * @param {string} nodeId id of the zwave device
     * @param {string} comClass parameter class
     * @param {string} value new value of the device class
     * @private
     */
    _onValueAdded (nodeId, comClass, value) {
        const node = this._nodes[nodeId]

        if (!node.classes[comClass]) {
            node.classes[comClass] = {}
        }
        node.classes[comClass][value.index] = value
        // do not publish an added value. The value is the default value and not the value of
        // the device.
    }

    /**
     * Handles a value removed event by removing the value from the node tree
     * @param {string} nodeId id of the zwave device
     * @param {string} comClass parameter class
     * @param {string} index value index
     * @private
     */
    _onValueRemoved (nodeId, comClass, index) {
        const node = this._nodes[nodeId]
        if (node.classes[comClass] && node.classes[comClass][index]) {
            delete node.classes[comClass][index]
        }
    }

    /**
     * Sets a value of a zwave object
     * @param {object} zwaveObject zwave object to set the value to
     * @param {string|number|boolean} value value to set
     * @private
     */
    _setValue (zwaveObject, value) {
        if (zwaveObject.read_only) {
            throw Error('trying to set a value to a read only device')
        }
        if (zwaveObject.type === 'bool' || zwaveObject.type === 'switch') {
            value = value === 'on' || value === '1' || value === 1 || value === true || value === 'true'
        }
        if (zwaveObject.type === 'byte') {
            value = Number(value)
        }
        this._zwave.setValue(zwaveObject.node_id, zwaveObject.class_id, zwaveObject.instance, zwaveObject.index, value)
    }

    /**
     * Sets a parameter of a zwave node
     * @param {number} nodeId id of the node
     * @param {number} paramId id of the parameter to set
     * @param {number} value parameter value
     * @private
     */
    _setConfigParam (nodeId, paramId, value) {
        this._zwave.setConfigParam(nodeId, paramId, value)
    }

    /**
     * Sets a value
     * @param {string} topic name of the topic
     * @param {string|number|boolean} value to set
     */
    setValue (topic, value) {
        try {
            const topicArray = topic.split('/')
            const set = topicArray.pop()
            const objectLabel = topicArray.pop()
            const deviceTopic = topicArray.join('/')
            if (set !== 'set') {
                throw Error('set expected as last element in topic ' + topic)
            }
            const device = this._zwaveDevices.topicToZwaveId(this._nodes, deviceTopic, objectLabel)
            if (device === undefined) {
                throw Error('zwave set value with unknown topic ' + topic)
            }
            if (device.class_id === ZWAVE_CONFIGURATION) {
                this._setConfigParam(device.node_id, device.index, value)
            } else {
                this._setValue(device, value)
            }
        } catch (err) {
            errorLog(err)
        }
    }

    /**
     * Enable adding devices
     */
    addDevice () {
        // For Openzwave < Version 1.3 use beginControllerCommand
        // this._zwave.beginControllerCommand('AddDevice', true)
        this._zwave.addNode(false)
    }

    /**
     * Remove/delete a failed node or device
     */
    removeFailedNode(node_id) {
        this._zwave.removeFailedNode(node_id)
    }

    /**
     * Requests all configuration parameter for a dedicated node
     * @param {value} node_id 
     * @private
     */
    _requestAllConfigParameter(node_id) {
        this._zwave.requestAllConfigParams(node_id)
    }

    /**
     * Iterates through all configurated nodes and requests their configuration parameters
     */
    requestConfigParametersForAllNodes() {
        this._zwaveDevices._devices.iterate('node_id', (node_id) => {
            this._requestAllConfigParameter(node_id)
        })
    }

    /**
     * Disconnects from usb device
     */
    async close () {
        this._zwave.disconnect(this._config.device)
    }
}

module.exports = ZwaveController
