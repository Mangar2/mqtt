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

const types = require('@mangar2/types')
const checkRule = require('./checkrule')
const Message = require('@mangar2/message')

/**
 * @typedef {Object} Rule
 * @property {string} name name of the rule
 * @property {object|string} time time, when the rule will be activated
 * @property {integer} doLog true, if the rule shall be logged
 */

/**
 * @description Creates an object holding automation rules
 * @param {Object} rulesTree rules tree
 * @param {CheckInput} check definition to check the validity of a rule
 * @example
 * const rulesTree =
 *  {
 *       location1: {
 *           rules: {
 *               rule1: {
 *                   title: 'hello'
 *               },
 *               rule2: {
 *                   title: 'world'
 *               }
 *           }
 *       },
 *       location2: {
 *           rule1: {
 *               error: 'faulty rule'
 *           }
 *       }
 *   }
 * const checkRule = new CheckInput({ type: 'object', properties: { title: { type: string } }, required: ['title'] })
 * const rules = new Rules(rulesTree, checkRules)
 * console.log(rules.rules.length) // prints 1, we have one correct rule
 * console.log(rules.invalidRules[0].messages) // prints the error messages for rule 'location2'
 */
class Rules {
    constructor(rulesTree) {
        this._rules = {}
        this._parseRules(rulesTree)
    }

    /**
     * @description Get the valid rules list
     * @returns {Rule[]} array of rules
     */
    getRules() {
        const result = []
        for (const index in this._rules) {
            const rule = this._rules[index]
            result.push(rule)
        }
        return result
    }

    /**
     * @description Get the invalid rules list
     * @returns {Rule[]} array of invalid rules 
     */
    getInvalidRules() {
        const result = []
        for (const index in this._rules) {
            const rule = this._rules[index]
            if (rule.isValid === false) {
                result.push(rule)
            }
        }
        return result
    }

    /**
     * Finds a rule in the rule list
     * @param {string} name name of the rule
     * @returns {Rule | null} rule found or null
     */
    findRule(name) {
        for (const index in this._rules) {
            const rule = this._rules[index]
            if (rule.name === name) {
                return rule
            }
        }
        return null
    }

    /**
     * @description Invalidates a rule, because it generates an error
     * @param {string} name name of the rule to invalidate
     * @param {string[]} messages error messages
     * @param {boolean} validFlag false to flag the rule as invalid to not process it again
     */
    _invalidateRule(name, messages, validFlag = false) {
        if (!this._rules[name]) {
            return
        }
        this._rules[name].isValid = validFlag
        this._rules[name].errors = messages
    }

    /**
     * Deletes a rule
     * @param {string} name name of the rule to delete
     */
    deleteRule(name) {
        delete this._rules[name]
    }

    /**
     * Adds or updates a rule to the list of rules, checks its validity and sets a "isValid" property accordingly
     * @param {Rule} rule to set
     * @returns true, if the rule is valid
     */
    setRule(rule) {
        rule.isValid = checkRule.validate(rule)
        this._rules[rule.name] = rule
        if (!rule.isValid) {
            this._invalidateRule(rule.name, checkRule.messages)
        }
        return rule.isValid
    }

    /**
     * Adds a rule to a tree node recursively
     * @param {Object} node current tree node
     * @param {string[]} nameChunks remaining chunks of the rule name
     * @param {Rule} rule rule
     */
    _addToNodeRec(node, nameChunks, rule) {
        const chunks = [...nameChunks]
        if (chunks.length > 1) {
            const curChunk = chunks.shift()
            if (node[curChunk] === undefined) {
                node[curChunk] = {}
            }
            this._addToNodeRec(node[curChunk], chunks, rule)
        } else if (chunks.length === 1) {
            const curChunk = chunks[0]
            if (node['rules'] === undefined) {
                node['rules'] = {}
            }
            node['rules'][curChunk] = rule
        }
        return node
    }

    /**
     * Returns all rules in a tree of rules
     * @returns all rules organized in a tree based on the names 
     */
    getRuleTree() {
        const result = {}
        for (const index in this._rules) {
            const rule = this._rules[index]
            const nameChunks = rule.name.split('/')
            this._addToNodeRec(result, nameChunks, rule)
        }
        return result
    }

    /**
     * @private
     * @description Parses a tree of rules and puts every rule in an array
     * @param {Object} node node in the rule tree
     * @param {string} link path to current node in the rule tree
     */
    _parseRules(node, link = '') {
        if (types.isObject(node)) {
            const separator = link === '' ? '' : '/'
            for (const key in node) {
                if (key === 'rules') {
                    for (const ruleIndex in node[key]) {
                        const rule = node[key][ruleIndex]
                        rule.name = link === '' ? ruleIndex : link + separator + ruleIndex
                        this.setRule(rule)
                    }
                } else {
                    const childNode = node[key]
                    const childLink = link + separator + key
                    this._parseRules(childNode, childLink)
                }
            }
        }
    }

    /**
     * Check, if the list of used variables includes undefined variables
     * @private
     * @param {{[index:string]: string|number|undefined}} variables list of used variables
     * @returns {string|null} null if there are no unused variables, else string listing unused variables 
     */
    _hasUndefinedVariables(variables) {
        let result = ''
        let spacer = ''
        for (const name in variables) {
            if (variables[name] === undefined) {
                result += `${spacer} '${name}' is undefined`
                spacer = ', '
            }
        }
        return result === '' ? null : result
    }

    /**
     * Checks all rules for errors, prints errors to console
     * @param {ProcessRule} processRule algirithm to check the rules
     * @returns {{variables: { [index:string]: string|number}, messages: Message[]}} 
     * map of used variables (variable name/variable value) and error messages
     */
    checkRules(processRule) {
        let usedVariables = {}
        let messages = []
        for (const index in this._rules) {
            const rule = this._rules[index]
            try {
                if (rule.isValid !== false && rule.isActive !== false) {
                    delete rule.errors
                    const curVariables = processRule.determineNeededVariables(rule)
                    usedVariables = { ...usedVariables, ...curVariables }
                    const undefinedVariables = this._hasUndefinedVariables(curVariables)
                    if (undefinedVariables) {
                        // We set an error message to the rule, but we do not invalidate it.
                        // An invalidated rule will never be checked again and therefore
                        // we will never check, if the variables are now defined.
                        throw { message: undefinedVariables, validFlag: true }
                    }
                }
            } catch (err) {
                const msg = `Error in rule '${rule.name}': ${err.message}`
                this._invalidateRule(rule.name, [msg], err.validFlag)
                messages.push(new Message(`$SYS/automation/rules/${rule.name}`, msg, 'check rules' ))
            }
        }
        return { variables: usedVariables, messages }
    }

}

module.exports = Rules
