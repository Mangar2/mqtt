/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Provides an automation functionality based on JSON formatted automation rules
 */

'use strict'

const Automation = require('./automation')
const { readRules, readRulesFromFileStore } = require('./readrules')

/**
 * Prepares the automation
 * @param {Object} config automation configuration
 * @param {Automation} [automation=null] existing automation class
 * @returns {Automation} the prepared automation object
 */
async function prepare (config, automation = null) {
    if (!automation) {
        automation = new Automation(config)
    }

    let rules = await readRulesFromFileStore(automation.filestoreOptions)
    if (!rules) {
        console.log('read rules from files')
        rules = readRules(config.rules)
        // await saveRulesToFileStore(rules, config.filestore)
    }
    automation.setRules(rules)

    return automation
}

module.exports = {
    Automation,
    prepare
}
