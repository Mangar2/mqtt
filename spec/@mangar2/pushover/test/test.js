/**
 * ---------------------------------------------------------------------------------------------------
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * File:      test.js
 *
 * Author:      Volker Böhm
 * Copyright:   Volker Böhm
 * ---------------------------------------------------------------------------------------------------
 */

'use strict'

const Pushover = require('@mangar2/pushover')

const configExample = {
    host: 'yourhost',
    path: '/1/messages.json',
    port: 443,
    token: 'your pushover token',
    user: 'your pushover user',
    devices: ['your device', 'your other device'],
    subscriptions: {
        '$SYS/incident/#': 1
    }
}

console.log('This test needs a valid configuration, it will not run without change');

(async () => {
    try {
        const pushover = new Pushover(configExample)
        const result = await pushover.publish(
            { topic: 'your/topic', value: 1, reason: [{ message: 'test1' }] },
            configExample
        )
        console.log(result)
    } catch (err) {
        console.log(err)
    }
    console.log('success')
})()
