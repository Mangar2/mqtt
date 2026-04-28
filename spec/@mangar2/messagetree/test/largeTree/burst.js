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

const MessageTree = require('@mangar2/messagetree')
const Message = require('@mangar2/message')

function randomPath () {
    let result = ''
    for (let depth = 0; depth < 5; depth++) {
        result = result + Math.floor(Math.random() * 5) + '/'
    }
    return result
}

function addToTree (tree) {
    const value = Math.floor(Math.random() * 5)
    const reason = 'A' + Math.floor(Math.random() * 2)
    const message = new Message(randomPath(), value, reason)
    tree.addData(message)
}

module.exports = (burstAmount) => {
    const tree = new MessageTree({
        lengthForFurtherCompression: 3,
        upperBoundAddInMilliseconds: 1,
        lowerBoundAddInMilliseconds: 1
    })
    for (let i = 0; i < burstAmount; i++) {
        addToTree(tree)
        if (i % 1000000 === 0 && i !== 0) {
            console.log('stringify (%s):', i)
            console.log(JSON.stringify(tree).length)
        }
    }
    return tree
}
