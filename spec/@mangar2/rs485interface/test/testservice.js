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
const Message = require('@mangar2/message')
const { delay } = require('@mangar2/utils')

const Rs485Interface = require('../index')
const arduinoService = new Rs485Interface(
    {
        serialPortName: 'COM5',
        trace: 'internal',
        maxVersion: 1,
        addresses: {
            'myfloor/myroom1/mydevice/': 20,
            'myfloor/myroom2/mydevice/': 21,
            'myfloor/myroom3/mydevice/': 22,
            'test/device1/': 120,
            'test/device2/': 121,
            'test/device3/': 122,
            'undefined/test/main/': 127
        },
        topics: {
            'myfloor/myroom1/mydevice/switch/switch1': {
                command: 'X',
                value: 1,
                address: 20
            },
            'myfloor/myroom2/mydevice/switch/switch2': {
                command: 'X',
                value: 2,
                address: 22
            },
            'myfloor/myroom2/mydevice/switch/switch3': {
                command: 'X',
                value: 4,
                address: 22
            },
            'myfloor/myroom2/mydevice/switch/switch4': {
                command: 'X',
                value: 8,
                address: 22
            },
            'myfloor/myroom2/mydevice/switch/switch5': {
                command: 'X',
                value: 16,
                address: 22
            }
        }
    }
)

arduinoService.on('publish', (message) => {
    console.log('%s = %s', message.topic, message.value)
})

const main = async () => {
    console.log('this test only works with a microcontroller on rs485 bus having the right software installed')
    await arduinoService.run()
    await delay(20000, true)
    const traceMessage = new Message('$SYS/rs485Interface/trace/set', 'messages')
    arduinoService.processMessage(traceMessage)
    let message = new Message('myfloor/myroom1/mydevice/Arduino base settings/reporting delay in seconds/set', 1)
    arduinoService.processMessage(message)
    await delay(20000, true)
    message = new Message('myfloor/myroom1/mydevice/Arduino base settings/reporting delay in seconds/set', 4)
    arduinoService.processMessage(message)
    await delay(2000, true)
    message = new Message('myfloor/myroom1/mydevice/Arduino base settings/reporting delay in seconds/set', 4)
    arduinoService.processMessage(message)
    await delay(2000, true)
    message = new Message('myfloor/myroom2/mydevice/switch/switch2/set', 'on')
    arduinoService.processMessage(message)
    await delay(2000, true)
    message = new Message('myfloor/myroom2/mydevice/switch/switch2/set', 'off')
    arduinoService.processMessage(message)
    message = new Message('myfloor/myroom2/mydevice/switch/switch2/set', 'on')
    arduinoService.processMessage(message)
    await delay(2000, true)
    message = new Message('myfloor/myroom2/mydevice/switch/switch3/blink', 4)
    arduinoService.processMessage(message)
    await delay(2000, true)
    message = new Message('myfloor/myroom2/mydevice/switch/switch4/temporary', 2)
    arduinoService.processMessage(message)
    // arduinoService.close()
}

main()
