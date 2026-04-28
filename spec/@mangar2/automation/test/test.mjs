
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
import { serviceTest } from './service/test.mjs'
import historyTest from './history/test.js'
import makroTest from './makros/test.js'
import motionTest from './motions/test.js'

const runTests = async () => {
    historyTest()
    await serviceTest()
    makroTest()
    motionTest()
}

runTests()

