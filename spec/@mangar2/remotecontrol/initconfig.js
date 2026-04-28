/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

const readline = require('readline')
const fs = require('fs')
const path = require('path')

/**
 * Prints a query string and reads a line from stdin
 * @param {string} question question string before entering the text
 * @returns {string} line read
 */
async function readText (question) {
    const rwInterface = readline.createInterface({
        input: process.stdin,
        output: process.stdout
    })
    const result = new Promise((resolve, reject) => {
        rwInterface.question(question, (text) => {
            rwInterface.close()
            resolve(text)
        })
    })
    return result
}

/**
 * Creates a question string to ask for a variable value
 * @param {string} variableName name of a variable to ask for
 * @param {string} type variable type definition (as provided in JSON schemata)
 * @param {string} description variable description
 * @param {string} defaultValue value of the variable, if no variable is entered
 */
function createQuestion (variableName, type, description, defaultValue) {
    let question = 'Please enter ' + description + ' (' + type + ') '
    if (defaultValue !== undefined) {
        question += ' [Default: ' + defaultValue + ']'
    }
    question += ': '
    return question
}

/**
 * Returns enteredValue or defaultValue, if entered value is the empty string
 * @param {string} enteredValue value entered
 * @param {string} defaultValue default value
 * @returns {string}
 */
function getValue (enteredValue, defaultValue) {
    let result = enteredValue
    if (enteredValue === '' && defaultValue !== undefined) {
        result = defaultValue
    }
    return result
}

/**
 * Reads several strings from stdin
 * @param {string[]} propertyNames list of property names to read
 * @param {Object} schema JSON schema with an object definition and a property section containing all provided propertyNames
 * @param {Object} defaultValues default value structure matching the provided schema
 * @returns {Object} list of read values
 */
async function readObjectFromStdin (propertyNames, schema, defaultValues) {
    const result = {}
    for (const variableName of propertyNames) {
        const definition = schema.properties[variableName]
        const type = definition.type
        const description = definition.description
        const defaultValue = defaultValues[variableName]
        const question = createQuestion(variableName, type, description, defaultValue)
        const text = await readText(question)
        result[variableName] = getValue(text, defaultValue)
    }
    return result
}

/**
 * Gets the reference of a schema
 * @param {Object} schema JSON schema definition
 * @param {string} ref reference string
 */
function getRef (schema, ref) {
    let result = {}
    const defs = schema.$defs
    for (const element in defs) {
        if (defs[element].$id === ref) {
            result = defs[element]
        }
    }
    return result
}

/**
 * Reads several strings from stdin to an object tree
 * @param {string[]} propertyNames list of property names to read
 * @param {Object} schema JSON schema with an object definition and a property section containing all provided propertyNames
 * @param {Object} defaultValues default value structure matching the provided schema
 * @returns {Object} Read values
 */
async function readTreeFromStdin (propertyNames, schema, defaultValues) {
    let result = {}
    for (const property in propertyNames) {
        if (property === 'required') {
            result = await readObjectFromStdin(propertyNames.required, schema, defaultValues)
        } else {
            let subSchema = schema.properties[property]
            if (subSchema.$ref !== undefined) {
                subSchema = getRef(schema, subSchema.$ref)
            }
            const subDefault = defaultValues !== undefined ? defaultValues[property] : undefined
            result[property] = await readTreeFromStdin(propertyNames[property], subSchema, subDefault)
        }
    }

    return result
}

/**
 * Write the configuration to a file
 * @param {Object} configuration configuration to save
 * @param {string} fileName name of the file
 */
function writeConfig (configuration, fileName) {
    const configAsString = JSON.stringify(configuration, null, 4)
    const pathAndName = path.join(__dirname, fileName)

    fs.writeFileSync(pathAndName, configAsString, { flag: 'w' })
}

module.exports = { writeConfig, readTreeFromStdin }
