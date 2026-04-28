/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2023 Volker Böhm
 */
'use strict'

const VERBOSE = false

import Message from '@mangar2/message'
import { prepare } from '@mangar2/automation'
import  { KeyValueStore } from '@mangar2/keyvaluestore'
import { delay } from '@mangar2/utils'

import Testrun from '@mangar2/testrun'
const testrun = new Testrun(VERBOSE)

import { fileURLToPath } from 'url'
import { dirname } from 'path'

const __filename = fileURLToPath(import.meta.url)
const __dirname = dirname(__filename)

let keyValueStore

testrun.on('prepare', async testcase => {
    const port = keyValueStore.port
    if (testcase.config.filestore) {
        testcase.config.filestore.port = port
        if (testcase.rules) {
            await keyValueStore.writeFile('/automation/rules', testcase.rules)
        }
    }
    const automation = await prepare(testcase.config)
    return automation
})

const runTest = async (test, automation) => {
    if (test.rules) {
        automation.setRules(test.rules)
    }
    let messages
    if (test.message) {
        const mqttMessage = new Message(test.message.topic, test.message.value, test.message.reason)
        mqttMessage.qos = test.message.qos ? test.message.qos : 1
        messages = automation.handleMessage(mqttMessage)
    } else {
        messages = automation.processTasks().messages
    }
    if (test.delay) {
        await delay(test.delay)
    }
    const subscriptions = automation.getSubscriptions()
    const rules = await keyValueStore.readFile('/automation/rules')
    return { 
        automation,
        subscriptions,
        messages,
        rules
    }
}

testrun.on('run', runTest)

testrun.on('break', async (test, automation) => {
    runTest(test, automation)
})

export const serviceTest = async () => {
    keyValueStore = new KeyValueStore({
        port: '0',
        directory: __dirname
    })

    await keyValueStore.run()
    // required to be sure that the keyValueStore is listening
    // await delay(500)
    console.log('running history test')
    await testrun.asyncRun(['service-testdata', 'file-store-testdata'], __dirname, 9, 'js')
    await keyValueStore.close()
}
