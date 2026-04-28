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

const TestRun = require('@mangar2/testrun')
const burst = require('./burst')
const MessageTree = require('@mangar2/messagetree')

const VERBOSE = false
const testRun = new TestRun(VERBOSE)

async function testLargeTree () {
    testRun.unitTest.log('creating large tree, takes a while')
    const tree = burst(1000000)
    testRun.unitTest.log('transforming tree to JSON')
    const jsonTree = JSON.stringify(tree.getSection('', 6))
    testRun.unitTest.log('Length of tree: ' + jsonTree.length)
    // Check that the tree does not grow too large
    testRun.unitTest.assertTrue(jsonTree.length < 50000000)

    await tree.persist(__dirname, 'message')
    const newTree = new MessageTree(tree.options)
    newTree.readTreeFromPersistedFile(__dirname, 'message')
    testRun.unitTest.log('Deep equal compare of loaded tree, takes a while')
    testRun.unitTest.assertDeepEqual(tree, newTree, 'persist ok')
    testRun.unitTest.showResult(2)
}

testLargeTree()
