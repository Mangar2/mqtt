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

const { errorLog, types } = require('@mangar2/utils')
const { HttpClient } = require('@mangar2/httpservice')
const fs = require('fs')

/**
 * Stores all rules to the filestore
 * @param {object} rules list of rules to store
 * @param {object} filestoreOptions 
 * @param {string} filestoreOptions.port port of the filestore
 * @param {string} filestoreOptions.host host of the filestore
 * @param {string} filestoreOptions.path path to store the data to
 */
async function saveRulesToFileStore(rules, filestoreOptions) {
    try {
        const client = new HttpClient(filestoreOptions.host, filestoreOptions.port)
        const result = await client.post( { path: filestoreOptions.path, payload: rules, type: 'json' })
        if (result.statusCode !== 200) {
            throw(result)
        }
    } catch (err) {
        console.log('Could not store rules to filetore')
        errorLog(err)
    }
}

/**
 * Reads all rules from filestore
 * @param {object} filestoreOptions 
 * @param {string} filestoreOptions.port port of the filestore
 * @param {string} filestoreOptions.host host of the filestore
 * @param {string} filestoreOptions.path path to store the data to
 * @param {boolean} filestoreOptions.use true, if filestore shall be used
 * @returns {object | null} rules read
 */
async function readRulesFromFileStore(filestoreOptions) {
    let result = null
    if (filestoreOptions.use) {
        try {
            const client = new HttpClient(filestoreOptions.host, filestoreOptions.port)
            const getResult = await client.getRequest(filestoreOptions.path)
            if (getResult.statusCode === 200) {
                result = JSON.parse(getResult.payload)
            } else {
                if (getResult.statusCode === 400) {
                    throw 'file not found'
                } else {
                    throw getResult.statusCode
                }
            }
        } catch (err) {
            result = null
            errorLog(`Could not read rules from filestore, fallback to config files: ${err}`)
        }
    }
    return result
}

/**
 * @private
 * @description Get/reads rules from files
 * @param {string|Array} rules rules information.
 * If type is string, rules are read from a file with the filename
 * If type is Array, rules are read from an array of files (filenames)
 * @returns {Object} set of rules
 */
function readRules (filenames) {
    let result = {}
    if (types.isString(filenames)) {
        result = JSON.parse(fs.readFileSync(filenames))
    } else if (types.isArray(filenames)) {
        result = {}
        try {
            for (const fileName of filenames) {
                const fileRules = JSON.parse(fs.readFileSync(fileName))
                result = { ...result, ...fileRules }
            }
        } catch (err) {
            errorLog(err)
        }
    }
    return result
}

/**
 * Watches rule files and reloads them on change
 * @param {Array|string} filenames name of the rule file(s)
 */
/*
function watchRules (filenames) {
    if (types.isString(filenames)) {
        filenames = [filenames]
    }
    for (const filename of filenames) {
        fs.watch(filename, { }, (eventType, filename) => {
            const rules = readRules(filenames)
            automation = new automation(config.automation, rules)
            const subscriptions = automation.getSubscriptions()
            mqttClient.subscriptions = subscriptions
        })
    }
}
*/

module.exports = {
    readRules,
    saveRulesToFileStore,
    readRulesFromFileStore
}
