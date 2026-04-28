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

console.log('testing serialmessage ...')
require('./testserialmessage')

console.log('testing rs485state ...')
require('./state/teststate')

console.log('testing rs485schedule')
require('./rs485/testrs485')

console.log('testing testread')
require('./testread')

console.log('testing arduinodns')
require('./serialdns/testserialdns')

console.log('testing actions')
require('./actions/testactions')

console.log('testing subscribe')
require('./subscribe/testsubscribe')

console.log('Several tests run in parallel, please wait about 30 seconds for all tests to finish')
