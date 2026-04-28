/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * This function implements a cleanup of the message tree
 * Elements with no actual message are deleted
 */

'use strict'

/**
 * Removes all outdated nodes. An outdated node is a node with no update in the last days and without any 
 * child with an update in the last days
 * @param {Node} tree message treee
 * @param {number} tree.time timestamp of the last change
 * @param {Object} three.childs object with a list of all direct childs of the tree
 * @param {number} daysWithoutUpdateToBeRemoved amount of days the last change is entered before a node is old 
 */
function cleanNodeRec (tree, daysWithoutUpdateToBeRemoved) {
    let result = null
    const childs = {}

    for (const childName in tree.childs) {
        const childNode = tree.childs[childName]
        const cleanedChildNode = cleanNodeRec( childNode, daysWithoutUpdateToBeRemoved )
        if (cleanedChildNode !== null) {
            childs[childName] = cleanedChildNode
        }
    }

    const ageInDays = ((new Date().getTime()) - tree.time) / (1000 * 3600 * 24)
    const hasChilds = Object.keys(childs).length > 0

    if (ageInDays <= daysWithoutUpdateToBeRemoved || hasChilds) {
        tree.childs = childs
        result = tree
    }

    return result
}

module.exports = cleanNodeRec