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

const SunnyPortal = require('@mangar2/sunnyportal')

const configExample = {
    user: 'your user',
    password: 'your password',
    host: 'www.sunnyportal.com',
    port: 443,
    paths: {
        login: '/Templates/Start.aspx',
        logout: '/Templates/Logout.aspx',
        production: '/Dashboard?_=1'
    }
}

console.log('This test needs a valid configuration, it will not run without change')
const sunnyPortal = new SunnyPortal(configExample);

(async () => {
    for (let i = 0; i < 10; i++) {
        const messages = await sunnyPortal.getMessages()
        for (const message of messages) {
            console.log(message)
        }
    }
})()
