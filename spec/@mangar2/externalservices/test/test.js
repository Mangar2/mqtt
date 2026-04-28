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

'use scrict'

const UnitTest = require('@mangar2/unittest')
const unitTest = new UnitTest();

(async () => {
    const mqttClient = require('../externalservices')
    await unitTest.delay(1000)
    await mqttClient.close()
    await unitTest.delay(1000)
})()
