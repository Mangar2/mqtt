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

const { SerialConnection } = require('../index')

async function test () {
    const serialConnection = new SerialConnection()
    await serialConnection.listAvailablePorts()
    try {
        await serialConnection.open('COM3', 1000000)
        let logCountdown = 100
        serialConnection.on('data', data => {
            const isZero = logCountdown === 0
            logCountdown--
            if (isZero) {
                console.log(data.toString())
                logCountdown = 100
            }
        })
        await serialConnection.sendData('Hello World ')
        serialConnection.close()
    } catch (err) {
        console.log(err)
        console.log('Error message is show, if COM5 is not available.')
    }
}

test()
