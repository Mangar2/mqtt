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
const { MessageQueues } = require('../dist/connections/messagequeues') // Update this import path

const VERBOSE = false
const unitTest = new UnitTest(VERBOSE)

/**
 * Function to perform a set of test cases based on test data loaded from a JSON object
 *
 * @param {UnitTest} unitTest - The instance of UnitTest class to perform assertions.
 * @param {Object[]} testData - Array of test cases loaded from a JSON object.
 */
function performGetMessagesToSendTests(unitTest, testData) {
    testData.forEach((testCase, index) => {
        const mq = new MessageQueues()

        testCase.entries.forEach(entryData => {
            const entry = new MessageQueueEntry({
                message: {
                    topic: entryData.topic,
                    value: entryData.value
                }
            })
            mq.addMessage(entry, testCase.maxQueueSize)
        })

        const messagesToSend = mq.getMessagesToSend(testCase.inFlightWindow, testCase.timeoutInMilliseconds)
        
        unitTest.assertEqual(messagesToSend.length, testCase.expectedLength, `Test case ${index + 1}: Expected ${testCase.expectedLength} messages`)
        
        const actualMessages = messagesToSend.map(entry => ({ topic: entry.message.topic, value: entry.message.value }))
        unitTest.assertDeepEqual(actualMessages, testCase.expectedMessages, `Test case ${index + 1}: Deep equal assertion`)
    })
}

// Test case data in JSON format
const getMessagesToSendTestData = [
    {
        entries: [
            { topic: 'topic1', value: 'value1' },
            { topic: 'topic1', value: 'value2' },
            { topic: 'topic1', value: 'value3' }
        ],
        maxQueueSize: 10,
        inFlightWindow: 2,
        timeoutInMilliseconds: 1000,
        expectedLength: 2,
        expectedMessages: [
            { topic: 'topic1', value: 'value1' },
            { topic: 'topic1', value: 'value2' }
        ]
    },
    {
        entries: [
            { topic: 'topic1', value: 'value1' },
            { topic: 'topic2', value: 'value2' }
        ],
        maxQueueSize: 10,
        inFlightWindow: 2,
        timeoutInMilliseconds: 1000,
        expectedLength: 2,
        expectedMessages: [
            { topic: 'topic1', value: 'value1' },
            { topic: 'topic2', value: 'value2' }
        ]
    }
]




module.exports = () => {

    // Test constructor
    const emptyQueues = new MessageQueues()
    unitTest.assertDeepEqual(emptyQueues.queues, {}, 'New MessageQueues instance should be empty')

    // Test fromJSON
    const jsonData = {
        _queues: {
            'topic1': [{ message: { topic: 'topic1', value: 'value1' } }],
            'topic2': [{ message: { topic: 'topic2', value: 'value2' } }]
        }
    }
    const queuesFromJSON = MessageQueues.fromJSON(jsonData)
    unitTest.assertEqual(Object.keys(queuesFromJSON.queues).length, 2, 'MessageQueues should be reconstructed from JSON data with 2 queues')

    // Test addMessage
    const entry1 = new MessageQueueEntry({ message: { topic: 'topic3', value: 'value3' } })
    queuesFromJSON.addMessage(entry1, 5)
    unitTest.assertEqual(queuesFromJSON.queues['topic3'].length, 1, 'MessageQueues should contain one entry in topic3 after adding')

    // Test addMessage with maxQueueSize parameter
    const entry2 = new MessageQueueEntry({ message: { topic: 'topic4', value: 'value4' } })
    const entry3 = new MessageQueueEntry({ message: { topic: 'topic4', value: 'value5' } })
    const entry4 = new MessageQueueEntry({ message: { topic: 'topic4', value: 'value6' } })
    queuesFromJSON.addMessage(entry2, 2)
    queuesFromJSON.addMessage(entry3, 2)
    queuesFromJSON.addMessage(entry4, 2)
    unitTest.assertEqual(queuesFromJSON.queues['topic4'].length, 2, 'MessageQueues should contain two entries in topic4 after adding three entries with maxQueueSize set to 2')
    unitTest.assertEqual(queuesFromJSON.queues['topic4'][0], entry3, 'First entry should be entry3 after exceeding maxQueueSize')
    unitTest.assertEqual(queuesFromJSON.queues['topic4'][1], entry4, 'Second entry should be entry4 after exceeding maxQueueSize')

    // Test acknowledgeMessageById
    queuesFromJSON.acknowledgeMessageById('topic3', entry1.packetid)
    unitTest.assertEqual(queuesFromJSON.queues['topic3'].length, 0, 'MessageQueues should contain zero entries in topic3 after acknowledging')

    // Test getMessagesToSend
    const messagesToSend = queuesFromJSON.getMessagesToSend(2, 1000)
    unitTest.assertTrue(Array.isArray(messagesToSend), 'getMessagesToSend should return an array')

    // Test getMessagesToSend
    performGetMessagesToSendTests(unitTest, getMessagesToSendTestData)
    
    // Test getMaxRetryCount
    const maxRetryCount = queuesFromJSON.getMaxRetryCount()
    unitTest.assertTrue(typeof maxRetryCount === 'number', 'getMaxRetryCount should return a number')

    // Test clearRetryCount
    queuesFromJSON.clearRetryCount()
    unitTest.assertEqual(queuesFromJSON.getMaxRetryCount(), 0, 'Max retry count should be 0 after clearing retry counts')
    return unitTest.getResultFunctions(14)
}
