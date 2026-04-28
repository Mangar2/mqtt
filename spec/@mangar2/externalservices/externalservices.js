/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Yaha microservice supporting external connections
 * @description
 * You need to provide a configuration file called "yahaconfig.json" stored in the root directory.
 * You need to provide the configuration for all used modules services and for the mqttclient
 * (see mqttclient, opensensemap, pushover, sunnyportal modules). Modules to run:
 * - opensensemap - send data to the opensensemap
 * - pushover - send data to the pushover app
 * - sunnyportal - reads data from sunnyportal
 * - remoteService - supports to invoke messages from the internet
 */

'use strict'

const MqttClient = require('@mangar2/mqttclient')
const getConfiguration = require('./configuration')

// path to your configuration file
const config = getConfiguration('yahaconfig.json')
const mqttClient = new MqttClient(config.externalServices)
const registerRemoteService = require('./registerRemoteService')
const registerOpensensemap = require('./registeropensensemap')
const registerPushover = require('./registerpushover')
const registerSunnyportal = require('./registersunnyportal')

mqttClient.run()

registerOpensensemap(mqttClient, config)
registerPushover(mqttClient, config)
registerSunnyportal(mqttClient, config)
registerRemoteService(mqttClient, config)

// Only for debugging, this is a main program

module.exports = mqttClient
