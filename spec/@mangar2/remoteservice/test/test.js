/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

const RemoteSerivce = require('../remoteservice')
const { HttpClient } = require('@mangar2/httpservice')
const UnitTest = require('@mangar2/unittest')
const VERBOSE = true
const DEBUG = false
const unitTest = new UnitTest(VERBOSE, DEBUG);

(async () => {
    const httpClient = new HttpClient('127.0.0.1', 9002)
    const remoteService = new RemoteSerivce({ server: { port: 9002 }, services: [{ path: '/testpath', reason: 'test', devices: { a: 'topica' } }] })
    remoteService.on('publish', (message) => {
        unitTest.assertEqual(message.topic, 'topica')
        unitTest.assertEqual(message.value, 'valuea')
        unitTest.assertEqual(message.reason[0].message, 'test')
    })
    remoteService.run()
    await httpClient.send('/testpath', 'POST', { deviceId: 'a', state: 'valuea', deviceToken: 'tokena' }, {})
    await unitTest.delay(100)
    await remoteService.close()
    unitTest.showResult(3)
})()
