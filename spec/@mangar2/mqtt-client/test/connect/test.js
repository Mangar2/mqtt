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

const VERBOSE = true

const Client = require('@mangar2/mqttclient')
const { HttpServer } = require('@mangar2/httpservice')
const Message = require('@mangar2/message')
const { delay } = require('@mangar2/utils')

const Testrun = require('@mangar2/testrun')
const testrun = new Testrun(VERBOSE)

class TestBroker {

    constructor() {
        this.server = new HttpServer(0)
        this.data = []
    }

    async run() {
        this.server.listen()

        this.server.on('put', (payload, headers, path, res) => {
            const param = { payload: JSON.parse(payload), headers, path }
            this.data.push(param)
            if (path === '/connect') {
                res.writeHead(200, { version: '1.0', packet: 'connack', 'Content-Type': 'application/json'} )
                res.end(JSON.stringify({ token: 'hello world' }))
    
            } else if ( path === '/disconnect') {
                res.writeHead(204, {})
                res.end('')
            } else if ( path === '/subscribe') {
                const { packetid } = headers
                res.writeHead(200, { version: '1.0', packetid, packet: 'suback', 'Content-Type': 'application/json'} )
                res.end(JSON.stringify({ clientId: 'test'}))
            } else if ( path === '/pingreq') {
                res.writeHead(204, { version: '1.0', packet: 'pingresp'} )
                res.end('')                
            } else {
                res.writeHead(200, {})
                res.end('')
            }
        })
    }

    get port() { return this.server.address.port }

    async close() {
        await this.server.close()
    }
}

testrun.on('prepare', async testcase => {
    const testobject = {
        testBroker: new TestBroker(0),
        client: null
    }
    await testobject.testBroker.run()
    testcase.options.broker.port = testobject.testBroker.port
    testobject.client = new Client(testcase.options)
    await testobject.client.run(0)
    return testobject
})

const runTest = async (test, testobject) => {
    if (test.recipient) {
        await testobject.client.registerRecipient('test', test.recipient, message => console.log(message))
    }
    if (test.publish) {
        const message = new Message(test.publish.topic, test.publish.value)
        message.qos = test.publish.qos
        // await testobject.client.publish(message)
    }
    await delay(200)
    return testobject.testBroker.data
}

testrun.on('run', runTest)

testrun.on('break', async (test, automation) => {
    runTest(test, automation)
})

testrun.on('cleanup', async testobject => {
    await testobject.client.close()
    await testobject.testBroker.close()
})

module.exports = async () => { testrun.asyncRun(['connect-test'], __dirname, 3, 'js') }
