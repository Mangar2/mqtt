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

const { MessageStore } = require('../index')
const Message = require('@mangar2/message')
const store = new MessageStore({ persist: { interval: 0 } });

(async () => {
    store.run()
    store.handleMessage(new Message('a', 1, 'Hello World'))
    await store.close()
    console.log('passed')
})()
