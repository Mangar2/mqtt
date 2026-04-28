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
const { UnitTest } = require('@mangar2/unittest')
const { MessageQueueEntry } = require('../dist/connections/messagequeueentry') // Update this import path
const { MessageQueue } = require('../dist/connections/messagequeue') // Update this import path

const VERBOSE = false
const unitTest = new UnitTest(VERBOSE)

module.exports = () => {

    // Test constructor
    const emptyQueue = new MessageQueue()
    unitTest.assertEqual(emptyQueue.queue.length, 0, 'New MessageQueue instance should be empty')

    // Test Getter and Setter
    const entry1 = new MessageQueueEntry({ message: { topic: 'topic1', value: 'value1' } })
    emptyQueue.queue = [entry1]
    unitTest.assertEqual(emptyQueue.queue.length, 1, 'MessageQueue should contain one entry after setting')

    // Test fromJSON
    const jsonData = {
        _queue: [
            { message: { topic: 'topic2', value: 'value2' } }
        ]
    }
    const queueFromJSON = MessageQueue.fromJSON(jsonData)
    unitTest.assertEqual(queueFromJSON.queue.length, 1, 'MessageQueue should be reconstructed from JSON data')

    // Test addMessage
    const entry2 = new MessageQueueEntry({ message: { topic: 'topic3', value: 'value3' } })
    queueFromJSON.addMessage(entry2, 2)
    unitTest.assertEqual(queueFromJSON.queue.length, 2, 'MessageQueue should contain two entries')

    const entry3 = new MessageQueueEntry({ message: { topic: 'topic4', value: 'value4' } })
    queueFromJSON.addMessage(entry3, 2)
    unitTest.assertEqual(queueFromJSON.queue.length, 2, 'MessageQueue should maintain max length after adding')

    // Test deleteAllEntries
    const deletedEntries = queueFromJSON.deleteAllEntries()
    unitTest.assertEqual(queueFromJSON.queue.length, 0, 'MessageQueue should be empty after deleting all entries')
    unitTest.assertEqual(deletedEntries.length, 2, 'Should return all deleted entries')
    return unitTest.getResultFunctions(7)
}
