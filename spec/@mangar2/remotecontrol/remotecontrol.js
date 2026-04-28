#!/usr/bin/env node

/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Service to run operating system commands remotely and continuously send keep alive messages.
 * It is used to include computers in the yaha home automation system. It can for example be used
 * to shutdown a computer. Then switch of the power remotely once keep alive messages are
 * no longer received.
 * For security, it will only support remote commands configured in a configuration file, it
 * will not directly process message content (send as string) on a remote computer.
 */

'use strict'

const MqttClient = require('@mangar2/mqttclient')
const readConfig = require('./configuration')
const { errorLog } = require('@mangar2/utils')
const Message = require('@mangar2/message')

/**
 * @private
 * @description Registers the automation service, and starts the remote control
 * service
 */
async function startRemoteControl () {
    const config = await readConfig('yahaconfig.json')

    if (config) {
        try {
            const mqttClient = new MqttClient(config)

            const RunCommand = require('./runCommand')
            // watchRules()
            const runCommand = new RunCommand(config)
            runCommand.on('publish', (message) => mqttClient.publish(message))
            const subscribe = {}
            subscribe[config.subscribeTopic + '/set'] = config.subscribeQoS

            mqttClient.registerRecipient(subscribe, (message) => {
                runCommand.processMessage(message)
            })
            mqttClient.registerSender(config.statusIntervalInSeconds * 1000, async () => {
                const aliveMessage = new Message(config.subscribeTopic, 'alive', 'Alive broadcast')
                aliveMessage.qos = 0
                return aliveMessage
            })

            mqttClient.run()
            console.log('started runCommand service')
        } catch (err) {
            errorLog(err, false)
        }
    }
}

startRemoteControl()
