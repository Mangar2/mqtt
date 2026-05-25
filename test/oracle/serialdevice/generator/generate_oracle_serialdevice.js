#!/usr/bin/env node

'use strict'

const fs = require('fs')
const path = require('path')
const crypto = require('crypto')
const Module = require('module')

function parseArgs (argv) {
    const args = {
        config: null,
        out: null,
        families: ['A', 'B', 'C', 'D', 'E', 'F'],
        verifyDeterminism: false,
        updateManifest: false,
        failOnDiff: false
    }

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--config') {
            args.config = argv[++i]
        } else if (arg === '--out') {
            args.out = argv[++i]
        } else if (arg === '--families') {
            args.families = argv[++i].split(',').map(item => item.trim()).filter(Boolean)
        } else if (arg === '--verify-determinism') {
            args.verifyDeterminism = true
        } else if (arg === '--update-manifest') {
            args.updateManifest = true
        } else if (arg === '--fail-on-diff') {
            args.failOnDiff = true
        }
    }

    if (!args.config || !args.out) {
        throw new Error('Missing required arguments: --config <path> and --out <path>')
    }

    return args
}

function stableNormalize (value) {
    if (Array.isArray(value)) {
        return value.map(stableNormalize)
    }
    if (value && typeof value === 'object') {
        const result = {}
        const keys = Object.keys(value).sort()
        for (const key of keys) {
            result[key] = stableNormalize(value[key])
        }
        return result
    }
    return value
}

function stableStringify (value) {
    return JSON.stringify(stableNormalize(value), null, 2) + '\n'
}

function deepClone (value) {
    return JSON.parse(JSON.stringify(value))
}

function deepMerge (base, override) {
    const baseIsObject = base && typeof base === 'object' && !Array.isArray(base)
    const overrideIsObject = override && typeof override === 'object' && !Array.isArray(override)

    if (!baseIsObject || !overrideIsObject) {
        return deepClone(override)
    }

    const result = deepClone(base)
    for (const key of Object.keys(override)) {
        const baseValue = result[key]
        const overrideValue = override[key]
        const canMerge =
            baseValue && typeof baseValue === 'object' && !Array.isArray(baseValue) &&
            overrideValue && typeof overrideValue === 'object' && !Array.isArray(overrideValue)
        if (canMerge) {
            result[key] = deepMerge(baseValue, overrideValue)
        } else {
            result[key] = deepClone(overrideValue)
        }
    }
    return result
}

function makeCase (id, title, tags, configRef, input, expected, expectedError, notes, expectedOrder = null) {
    const testCase = {
        id,
        title,
        tags,
        configRef,
        input,
        notes
    }
    if (expectedError !== null) {
        testCase.expectedError = expectedError
    } else {
        testCase.expected = expected
    }
    if (expectedOrder !== null) {
        testCase.expectedOrder = expectedOrder
    }
    return testCase
}

function createSuite (suiteId, cfg, cases) {
    return {
        suiteId,
        oracleVersion: cfg.oracleVersion,
        legacySource: {
            module: cfg.legacyModule,
            commit: cfg.legacyCommit,
            generatorVersion: cfg.generatorVersion
        },
        createdAtUtc: cfg.createdAtUtc,
        deterministicSeed: cfg.deterministicSeed,
        cases
    }
}

function normalizeSerialMessage (message) {
    if (!message) {
        return null
    }
    return {
        interfaceName: message.interfaceName,
        sender: message.sender,
        receiver: message.receiver,
        command: message.command,
        value: message.value,
        action: message.action
    }
}

function normalizeMqttMessage (message) {
    return {
        topic: message.topic,
        value: message.value,
        reason: message.reason,
        qos: message.qos
    }
}

function ensureDirectory (dirPath) {
    fs.mkdirSync(dirPath, { recursive: true })
}

function readJson (filePath) {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'))
}

function writeTextIfChanged (filePath, content) {
    const exists = fs.existsSync(filePath)
    const oldContent = exists ? fs.readFileSync(filePath, 'utf8') : null
    const changed = oldContent !== content
    if (changed) {
        ensureDirectory(path.dirname(filePath))
        fs.writeFileSync(filePath, content, 'utf8')
    }
    return changed
}

function sha256Hex (content) {
    return crypto.createHash('sha256').update(content, 'utf8').digest('hex')
}

function sortCases (cases) {
    return cases.slice().sort((a, b) => a.id.localeCompare(b.id))
}

function createDefaultOptions () {
    return {
        qos: 1,
        serialPortName: '/dev/null',
        baudrate: 38400,
        trace: 'errors',
        keepAliveDelayInSeconds: 1,
        interfaces: {
            i2c: {
                commandMap: {
                    L: 'i2c/brightness sensor/brightness',
                    M: 'i2c/motion sensor/detection state',
                    T: 'i2c/temperature and humidity sensor/temperature in celsius'
                },
                receiverMap: {
                    'level0/room1/device1/': 3,
                    'level1/room1/device1/': 4,
                    '$SYS/central/': 'main'
                }
            },
            fs20: {
                commandMap: {
                    '12322324/2111': 'level0/room1/fs20/task/one'
                },
                sendMap: {
                    '12322324/2112': 'level0/room1/fs20/task/two'
                }
            },
            switch: {
                topicMap: {
                    'level0/room1/switch/one': {
                        command: 'switch',
                        value: 1,
                        address: 'main'
                    },
                    'level0/room1/switch/two': {
                        command: 'switch',
                        value: 2,
                        address: 'main'
                    }
                }
            },
            serial: {
                commandMap: {
                    t: 'Temperature and Humidity Sensor/temperature in celsius',
                    l: 'Light/light on time',
                    z: 'Arduino Status Information/memory left in bytes'
                },
                receiverMap: {
                    'level0/room1/device1/': 5,
                    'level0/room1/device2/': 6
                },
                valueMap: {
                    LightOnOff: {
                        description: 'Switches light on/off by setting the light on time in seconds',
                        usedby: ['V', 'l'],
                        map: {
                            on: 3600,
                            off: 0
                        }
                    }
                }
            }
        }
    }
}

function installLegacyShims () {
    const state = {
        delayHook: null,
        serialBehavior: {
            sent: [],
            sendFailCount: 0,
            openFailCount: 0,
            listCalls: 0
        }
    }

    class Message {
        constructor (topic, value, reason = '') {
            this.topic = topic
            this.value = value
            this.reason = reason
            this.qos = 0
        }

        addReason (reason) {
            if (!this.reason) {
                this.reason = reason
            } else {
                this.reason += '; ' + reason
            }
        }
    }

    class TaskQueue {
        constructor () {
            this._taskCb = null
        }

        on (event, cb) {
            if (event === 'task') {
                this._taskCb = cb
            }
        }

        addTask (data) {
            if (this._taskCb) {
                Promise.resolve(this._taskCb(data)).catch(() => {})
            }
        }
    }

    class Callbacks {
        constructor (events) {
            this._events = new Set(events.map(item => item.toLowerCase()))
            this._callbacks = {}
        }

        on (event, cb) {
            if (typeof cb !== 'function') {
                throw Error('Callback must be a function')
            }
            const normalized = String(event).toLowerCase()
            if (!this._events.has(normalized)) {
                throw Error('Unsupported event: ' + event)
            }
            this._callbacks[normalized] = cb
        }

        invokeCallback (event, data) {
            const normalized = String(event).toLowerCase()
            const cb = this._callbacks[normalized]
            if (cb) {
                cb(data)
            }
        }
    }

    class FakeSerialConnection {
        constructor () {
            this._handlers = {}
            this._open = false
        }

        on (event, cb) {
            this._handlers[event] = cb
        }

        async open () {
            if (state.serialBehavior.openFailCount > 0) {
                state.serialBehavior.openFailCount--
                throw Error('open failed')
            }
            this._open = true
        }

        async close () {
            this._open = false
        }

        async sendData (data) {
            state.serialBehavior.sent.push(data)
            if (state.serialBehavior.sendFailCount > 0) {
                state.serialBehavior.sendFailCount--
                throw Error('send failed')
            }
        }

        async listAvailablePorts () {
            state.serialBehavior.listCalls++
        }

        emitData (data) {
            if (this._handlers.data) {
                this._handlers.data(data)
            }
        }
    }

    class MatchMessages {
        addReceivedMessage () {}
        hasMatchingMessage () { return false }
        matchAndUpdateReplyMessage (message) { return message }
    }

    class CheckInput {
        constructor () {}
    }

    function sanitizeConfiguration (config, defaults) {
        return deepMerge(defaults, config)
    }

    const types = {
        isObject: value => value !== null && typeof value === 'object' && !Array.isArray(value),
        isInteger: Number.isInteger,
        isString: value => typeof value === 'string' || value instanceof String,
        isArray: Array.isArray
    }

    const shimByPackage = {
        '@mangar2/message': Message,
        '@mangar2/utils': {
            types,
            errorLog: () => {},
            delay: async (ms) => {
                if (typeof state.delayHook === 'function') {
                    await state.delayHook(ms)
                }
            },
            TaskQueue,
            Callbacks
        },
        '@mangar2/configuration': sanitizeConfiguration,
        '@mangar2/errorlog': () => {},
        '@mangar2/checkinput': CheckInput,
        '@mangar2/serialhelper': {
            SerialConnection: FakeSerialConnection
        },
        '@mangar2/matchmessages': MatchMessages
    }

    const originalLoad = Module._load
    Module._load = function (request, parent, isMain) {
        if (Object.prototype.hasOwnProperty.call(shimByPackage, request)) {
            return shimByPackage[request]
        }
        return originalLoad.call(this, request, parent, isMain)
    }

    return {
        restore: () => {
            Module._load = originalLoad
        },
        state,
        Message
    }
}

function getLegacyModules (legacyRoot) {
    const localModules = [
        'parseserialdata.js',
        'mqtttoserial.js',
        'serialtomqtt.js',
        'serialmessage.js',
        'serialmessagetostring.js',
        'derivesubscribes.js',
        'serialdevice.js',
        'configuration.js',
        'constants.js'
    ]

    for (const moduleName of localModules) {
        const modulePath = path.join(legacyRoot, moduleName)
        delete require.cache[require.resolve(modulePath)]
    }

    return {
        ParseSerialData: require(path.join(legacyRoot, 'parseserialdata.js')),
        MQTTToSerial: require(path.join(legacyRoot, 'mqtttoserial.js')),
        SerialToMQTT: require(path.join(legacyRoot, 'serialtomqtt.js')),
        SerialMessage: require(path.join(legacyRoot, 'serialmessage.js')),
        serialMessageToString: require(path.join(legacyRoot, 'serialmessagetostring.js')).serialMessageToString,
        deriveSubscribes: require(path.join(legacyRoot, 'derivesubscribes.js')).deriveSubscribes,
        SerialDevice: require(path.join(legacyRoot, 'serialdevice.js'))
    }
}

function buildFamilyA (cfg, modules) {
    const { ParseSerialData } = modules

    const createExpected = (chunks) => {
        const parser = new ParseSerialData()
        const result = []
        for (const chunk of chunks) {
            const output = parser.parse(Buffer.from(chunk))
            if (output !== null) {
                result.push(normalizeSerialMessage(output))
            }
        }
        return result
    }

    const coreCases = sortCases([
        makeCase(
            'A-001',
            'Parses i2c array message',
            ['array', 'i2c'],
            'default',
            { chunks: ['[3,"M",1]'] },
            createExpected(['[3,"M",1]']),
            null,
            'Single array payload should become one i2c serial message'
        ),
        makeCase(
            'A-002',
            'Skips noise then parses object format',
            ['noise', 'object'],
            'default',
            { chunks: ['xyz', '{"S":5,"R":6,"K":"t","V":21}'] },
            createExpected(['xyz', '{"S":5,"R":6,"K":"t","V":21}']),
            null,
            'Noise before first object bracket is ignored'
        ),
        makeCase(
            'A-003',
            'Parses fs20 incoming format with set action',
            ['fs20'],
            'default',
            { chunks: ['{"Hauscode":"1234","Adresse":"1111","Befehl":0}'] },
            createExpected(['{"Hauscode":"1234","Adresse":"1111","Befehl":0}']),
            null,
            'FS20 object should map to action /set and off value'
        )
    ])

    const malformedCases = sortCases([
        makeCase(
            'A-101',
            'Malformed object is dropped and next frame is parsed',
            ['malformed', 'recovery'],
            'default',
            { chunks: ['{bad}', '[3,"L",42]'] },
            createExpected(['{bad}', '[3,"L",42]']),
            null,
            'Malformed JSON is ignored after parse failure, parser continues'
        )
    ])

    return {
        'A-parser/core.json': createSuite('A-parser-core', cfg, coreCases),
        'A-parser/malformed.json': createSuite('A-parser-malformed', cfg, malformedCases)
    }
}

function buildFamilyB (cfg, modules, defaultOptions) {
    const { MQTTToSerial } = modules
    const mapper = new MQTTToSerial(defaultOptions)

    const toExpected = (topic, value) => {
        try {
            const serialMessage = mapper.toSerialMessage({ topic, value })
            return { expected: normalizeSerialMessage(serialMessage), expectedError: null }
        } catch (err) {
            return { expected: null, expectedError: err.message }
        }
    }

    const coreCases = sortCases([
        (() => {
            const result = toExpected('level0/room1/switch/one', 'on')
            return makeCase(
                'B-001',
                'Maps switch topic to switch serial command on',
                ['switch', 'topicMap'],
                'default',
                { topic: 'level0/room1/switch/one', value: 'on' },
                result.expected,
                result.expectedError,
                'Direct topicMap mapping path for switch command'
            )
        })(),
        (() => {
            const topic = 'level0/room1/device1/i2c/brightness sensor/brightness'
            const result = toExpected(topic, '77')
            return makeCase(
                'B-002',
                'Maps i2c command by topic suffix and receiver prefix',
                ['suffix-route', 'receiver-prefix'],
                'default',
                { topic, value: '77' },
                result.expected,
                result.expectedError,
                'Fallback command suffix path with integer conversion'
            )
        })(),
        (() => {
            const topic = 'level0/room1/device1/Light/light on time'
            const result = toExpected(topic, 'off')
            return makeCase(
                'B-003',
                'Maps serial value by valueMap',
                ['valueMap'],
                'default',
                { topic, value: 'off' },
                result.expected,
                result.expectedError,
                'Value map converts off to 0 for command l'
            )
        })()
    ])

    const errorCases = sortCases([
        (() => {
            const result = toExpected('unknown/topic', '1')
            return makeCase(
                'B-101',
                'Throws on unknown topic mapping',
                ['error', 'unknown-topic'],
                'default',
                { topic: 'unknown/topic', value: '1' },
                result.expected,
                result.expectedError,
                'No matching command map entry'
            )
        })(),
        (() => {
            const topic = 'level0/room1/device1/i2c/brightness sensor/brightness'
            const result = toExpected(topic, 'invalid-number')
            return makeCase(
                'B-102',
                'Throws on non-integer mapped value',
                ['error', 'invalid-value'],
                'default',
                { topic, value: 'invalid-number' },
                result.expected,
                result.expectedError,
                'Value cannot be normalized to integer'
            )
        })()
    ])

    return {
        'B-mqtt-to-serial/core.json': createSuite('B-mqtt-to-serial-core', cfg, coreCases),
        'B-mqtt-to-serial/errors.json': createSuite('B-mqtt-to-serial-errors', cfg, errorCases)
    }
}

function buildFamilyC (cfg, modules, defaultOptions) {
    const { SerialToMQTT, SerialMessage } = modules
    const mapper = new SerialToMQTT(defaultOptions)

    const toExpected = (message) => {
        try {
            const serialMessage = new SerialMessage(
                message.interfaceName,
                message.sender,
                message.receiver,
                message.command,
                message.value,
                message.action || ''
            )
            const mqttMessages = mapper.toMqttMessages(serialMessage).map(normalizeMqttMessage)
            return { expected: mqttMessages, expectedError: null }
        } catch (err) {
            return { expected: null, expectedError: err.message }
        }
    }

    const coreCases = sortCases([
        (() => {
            const input = {
                interfaceName: 'serial',
                sender: 5,
                receiver: 0,
                command: 't',
                value: 22,
                action: ''
            }
            const result = toExpected(input)
            return makeCase(
                'C-001',
                'Maps serial temperature message to mqtt topic',
                ['serial', 'topic-derivation'],
                'default',
                input,
                result.expected,
                result.expectedError,
                'Normal serial command mapping path'
            )
        })(),
        (() => {
            const input = {
                interfaceName: 'fs20',
                sender: null,
                receiver: null,
                command: '12322324/2112',
                value: 'on',
                action: '/set'
            }
            const result = toExpected(input)
            return makeCase(
                'C-002',
                'Uses sendMap fallback for fs20 serial command',
                ['fs20', 'sendMap'],
                'default',
                input,
                result.expected,
                result.expectedError,
                'SerialToMQTT resolves topic from sendMap when commandMap misses'
            )
        })()
    ])

    const switchCases = sortCases([
        (() => {
            const input = {
                interfaceName: 'switch',
                sender: 'main',
                receiver: null,
                command: 'switch',
                value: 0x4001,
                action: ''
            }
            const result = toExpected(input)
            return makeCase(
                'C-101',
                'Switch command generates on event for addressed bit',
                ['switch', 'on'],
                'default',
                input,
                result.expected,
                result.expectedError,
                'SWITCH_ON + bit mask should publish on for switch one'
            )
        })(),
        (() => {
            const input = {
                interfaceName: 'switch',
                sender: 'main',
                receiver: null,
                command: 'switch',
                value: 0x0002,
                action: ''
            }
            const result = toExpected(input)
            return makeCase(
                'C-102',
                'Switch status frame publishes on/off for all configured bits',
                ['switch', 'status-frame'],
                'default',
                input,
                result.expected,
                result.expectedError,
                'No SWITCH_ON/OFF flag means full status projection'
            )
        })()
    ])

    return {
        'C-serial-to-mqtt/core.json': createSuite('C-serial-to-mqtt-core', cfg, coreCases),
        'C-serial-to-mqtt/switch.json': createSuite('C-serial-to-mqtt-switch', cfg, switchCases)
    }
}

function buildFamilyD (cfg, modules) {
    const { serialMessageToString } = modules

    const cases = sortCases([
        (() => {
            const input = { interfaceName: 'i2c', sender: null, receiver: 3, command: 'L', value: 77 }
            return makeCase(
                'D-001',
                'Serializes i2c command',
                ['i2c'],
                'default',
                input,
                serialMessageToString(input),
                null,
                'I2C serialization format C<receiver><command><value>'
            )
        })(),
        (() => {
            const input = { interfaceName: 'serial', sender: 1, receiver: 2, command: 't', value: 19 }
            return makeCase(
                'D-002',
                'Serializes serial JSON command',
                ['serial'],
                'default',
                input,
                serialMessageToString(input),
                null,
                'Serial format uses JSON keys S,R,C,V'
            )
        })(),
        (() => {
            const input = { interfaceName: 'switch', sender: null, receiver: 'main', command: 'switch', value: 0x4002 }
            return makeCase(
                'D-003',
                'Serializes switch on command with bit index',
                ['switch'],
                'default',
                input,
                serialMessageToString(input),
                null,
                'Switch format s<lsb><H|L>'
            )
        })(),
        (() => {
            const input = { interfaceName: 'fs20', sender: null, receiver: null, command: '12322324/2111', value: 'on' }
            return makeCase(
                'D-004',
                'Serializes fs20 command',
                ['fs20'],
                'default',
                input,
                serialMessageToString(input),
                null,
                'FS20 format G<commandPart><value>'
            )
        })()
    ])

    return {
        'D-wire-serialization/core.json': createSuite('D-wire-serialization-core', cfg, cases)
    }
}

function buildFamilyE (cfg, modules, defaultOptions) {
    const { deriveSubscribes } = modules

    const toExpected = (options) => deriveSubscribes(options)

    const cases = sortCases([
        makeCase(
            'E-001',
            'Derives subscriptions with receiver map and topic map',
            ['subscribes', 'receiverMap', 'topicMap'],
            'default',
            { options: defaultOptions },
            toExpected(defaultOptions),
            null,
            'Includes all command/topic subscriptions and system topic'
        ),
        (() => {
            const custom = deepClone(defaultOptions)
            custom.interfaces.serial.receiverMap = null
            return makeCase(
                'E-002',
                'Derives subscriptions without receiver map',
                ['subscribes', 'no-receiverMap'],
                'default',
                { options: custom },
                toExpected(custom),
                null,
                'Falls back to command map only subscriptions when receiverMap is missing'
            )
        })()
    ])

    return {
        'E-subscribes/core.json': createSuite('E-subscribes-core', cfg, cases)
    }
}

async function buildFamilyF (cfg, modules, defaultOptions, shimContext) {
    const { SerialDevice } = modules
    const { Message, state } = shimContext

    const keepAliveCases = []
    {
        const options = deepClone(defaultOptions)
        options.keepAliveDelayInSeconds = 0
        const device = new SerialDevice(options)
        const sent = []
        let delayCalls = 0
        state.delayHook = async () => {
            delayCalls++
        }
        device._sendDataToSerial = async (payload) => {
            sent.push(payload)
            device._close = true
        }
        await device._runSendKeepAlive()
        state.delayHook = null

        keepAliveCases.push(makeCase(
            'F-001',
            'Keep-alive loop sends at payload',
            ['runtime', 'keepalive'],
            'default',
            { keepAliveDelayInSeconds: 0 },
            { sent, delayCalls },
            null,
            'First keep-alive cycle must send exact payload at'
        ))
    }

    {
        const options = deepClone(defaultOptions)
        const device = new SerialDevice(options)
        const published = []
        device.on('publish', (message) => published.push(normalizeMqttMessage(message)))
        const incoming = new Message('demo/topic/set', '1', 'incoming')
        device._matchMessages = {
            addReceivedMessage: () => {},
            hasMatchingMessage: () => false,
            matchAndUpdateReplyMessage: (message) => message
        }
        device._mqttToSerial = {
            toSerialMessage: () => ({ interfaceName: 'serial', sender: null, receiver: 1, command: 't', value: 1 })
        }
        device._taskQueue = {
            addTask: () => {}
        }
        device.handleMessage(incoming)
        device._publish([new Message('demo/topic/set', '1', 'received from arduino')])

        keepAliveCases.push(makeCase(
            'F-002',
            'Publish path keeps set suffix when no reply match exists',
            ['runtime', 'publish', 'set-suffix'],
            'default',
            { topic: 'demo/topic/set', value: '1' },
            { published },
            null,
            'Set suffix is removed and re-added when not a matched reply'
        ))
    }

    const retryCases = []
    {
        const options = deepClone(defaultOptions)
        const device = new SerialDevice(options)
        let sendCalls = 0
        let openCalls = 0

        device._serial = {
            sendData: async () => {
                sendCalls++
                if (sendCalls <= 2) {
                    throw Error('send failed')
                }
            }
        }
        device._openSerialInterface = async () => {
            openCalls++
        }

        await device._sendDataToSerial('X')

        retryCases.push(makeCase(
            'F-101',
            'Retries send and reopens serial interface on errors',
            ['runtime', 'retry', 'open'],
            'default',
            { payload: 'X' },
            { sendCalls, openCalls },
            null,
            'Current logic performs reopen between retries until send succeeds'
        ))
    }

    {
        const options = deepClone(defaultOptions)
        const device = new SerialDevice(options)
        const topic = '$SYS/serialdevice/trace/set'
        device.handleMessage({ topic, value: 'internal', reason: 'oracle' })
        retryCases.push(makeCase(
            'F-102',
            'Trace topic updates runtime trace option',
            ['runtime', 'trace'],
            'default',
            { topic, value: 'internal' },
            { trace: device._options.trace },
            null,
            'Trace topic is a special control path and does not enqueue serial output'
        ))
    }

    return {
        'F-runtime-slices/keepalive.json': createSuite('F-runtime-slices-keepalive', cfg, sortCases(keepAliveCases)),
        'F-runtime-slices/retry-open.json': createSuite('F-runtime-slices-retry-open', cfg, sortCases(retryCases))
    }
}

async function generateSuites (cfg, families, legacyRoot) {
    const shimContext = installLegacyShims()
    const generated = {}

    try {
        const modules = getLegacyModules(legacyRoot)
        const defaultOptions = createDefaultOptions()

        for (const family of families) {
            if (family === 'A') {
                Object.assign(generated, buildFamilyA(cfg, modules))
            } else if (family === 'B') {
                Object.assign(generated, buildFamilyB(cfg, modules, defaultOptions))
            } else if (family === 'C') {
                Object.assign(generated, buildFamilyC(cfg, modules, defaultOptions))
            } else if (family === 'D') {
                Object.assign(generated, buildFamilyD(cfg, modules))
            } else if (family === 'E') {
                Object.assign(generated, buildFamilyE(cfg, modules, defaultOptions))
            } else if (family === 'F') {
                Object.assign(generated, await buildFamilyF(cfg, modules, defaultOptions, shimContext))
            } else {
                throw new Error('Unsupported family: ' + family)
            }
        }

        return generated
    } finally {
        shimContext.restore()
    }
}

function buildManifest (suiteContentByPath, cfg) {
    const fileEntries = Object.keys(suiteContentByPath)
        .sort()
        .map(filePath => {
            const content = suiteContentByPath[filePath]
            return {
                path: filePath,
                size: Buffer.byteLength(content, 'utf8'),
                sha256: sha256Hex(content)
            }
        })

    return {
        generatedAtUtc: cfg.createdAtUtc,
        files: fileEntries
    }
}

async function main () {
    try {
        const args = parseArgs(process.argv.slice(2))
        const cfg = readJson(args.config)

        const scriptDir = __dirname
        const repositoryRoot = path.resolve(scriptDir, '..', '..', '..', '..')
        const legacyRoot = path.resolve(repositoryRoot, cfg.legacyModule)
        const outRoot = path.resolve(repositoryRoot, args.out)

        const buildOnce = async () => {
            const suites = await generateSuites(cfg, args.families, legacyRoot)
            const fileContentByPath = {}
            for (const relativePath of Object.keys(suites)) {
                fileContentByPath[relativePath] = stableStringify(suites[relativePath])
            }
            if (args.updateManifest) {
                fileContentByPath.manifest = stableStringify(buildManifest(fileContentByPath, cfg))
            }
            return fileContentByPath
        }

        const firstBuild = await buildOnce()

        if (args.verifyDeterminism) {
            const secondBuild = await buildOnce()
            const allKeys = new Set([...Object.keys(firstBuild), ...Object.keys(secondBuild)])
            for (const key of allKeys) {
                if (firstBuild[key] !== secondBuild[key]) {
                    process.exitCode = 3
                    throw new Error('Determinism verification failed for ' + key)
                }
            }
        }

        let changedFiles = 0
        for (const relativePath of Object.keys(firstBuild)) {
            const isManifest = relativePath === 'manifest'
            const targetPath = isManifest
                ? path.join(outRoot, 'manifest.json')
                : path.join(outRoot, relativePath)
            const newContent = firstBuild[relativePath]
            const exists = fs.existsSync(targetPath)
            const oldContent = exists ? fs.readFileSync(targetPath, 'utf8') : null
            const changed = oldContent !== newContent

            if (changed && args.failOnDiff) {
                process.exitCode = 4
                throw new Error('Output diff detected for ' + path.relative(repositoryRoot, targetPath))
            }

            if (changed) {
                writeTextIfChanged(targetPath, newContent)
                changedFiles++
            }
        }

        console.log('Oracle generation complete. Updated files: ' + changedFiles)
    } catch (err) {
        if (!process.exitCode) {
            process.exitCode = 10
        }
        if (String(err.message).includes('schema')) {
            process.exitCode = 2
        }
        console.error(err.message)
    }
}

main()
