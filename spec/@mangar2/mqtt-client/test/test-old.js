/**
 * ---------------------------------------------------------------------------------------------------
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * Author:      Volker Böhm
 * Copyright:   Volker Böhm
 * ---------------------------------------------------------------------------------------------------
 */
'use strict'

const Client = require('@mangar2/mqttclient')
const { HttpServer } = require('@mangar2/httpservice')
const UnitTest = require('@mangar2/unittest')

const VERBOSE = true
const DEBUG = false
const unitTest = new UnitTest(VERBOSE, DEBUG)

const options = {
    clientId: 'unique',
    broker: { host: 'localhost', port: null },
    listener: 0,
    version: '1.0',
    keepAliveInSeconds: 300,
    clean: false,
    retry: 10,
    log: [{
        topic: '#',
        module: 'send',
        level: 1
    }]
}


async function test () {
    const server = new HttpServer(0)
    server.listen()
    options.broker.port = server.address.port
    const client = new Client(options)

    server.on('put', (payload, headers, path, res) => {
        const body = JSON.parse(payload)
        if (path === '/connect') {
            unitTest.assertEqual(body.clientId, options.clientId, 'connect/body/clientId')
            unitTest.assertTrue(body.port, 'connect/body/port')
            console.log(body)
            const payload = { 
                token: 'hello world'
            }
            res.writeHead(200, { version: '1.0', packet: 'connack', 'Content-Type': 'application/json'} )
            res.end(JSON.stringify(payload))

        } else if ( path === '/disconnect') {
            res.writeHead(204, {})
            res.end('')
        } else if ( path === '/subscribe') {

            const { packetid } = headers
            res.writeHead(200, { version: '1.0', packetid, packet: 'suback', 'Content-Type': 'application/json'} )
            res.end(JSON.stringify({ clientId: 'test'}))
        } else {
            console.log(path)        
            res.writeHead(200, {})
            res.end('')
        }
    })

    console.log('Host name and port will not be found "ENOTFOUND" errors expected')
    await client.run()
    await client.registerRecipient({ 'a/b': 1 }, (message) => console.log(message), () => {})
    await client.close()
    await server.close()
    unitTest.showResult(2)
}

test()
