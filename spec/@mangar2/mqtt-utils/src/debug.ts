/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * This package combines a list of utlities needed for mqtt based communications.
 */

import { Message } from './index'

/**
 * Creates a message showing the memory usage
 * @param clientId the client id
 * @returns { Message } the memory usage message with the topic $SYS/[clientId]/memory usage
 */
export function createMemoryUsageMessage(clientId: string) {
    const memory = process.memoryUsage()
    const BYTE_TO_MBYTE = 1000000
    const reason =
        'rss: ' + memory.rss / BYTE_TO_MBYTE + 'MB ' +
        'ht: ' + memory.heapTotal / BYTE_TO_MBYTE + 'MB ' +
        'hu: ' + memory.heapUsed / BYTE_TO_MBYTE + 'MB ' +
        'ext: ' + memory.external / BYTE_TO_MBYTE + 'MB ' +
        'aB: ' + memory.arrayBuffers
    const message = new Message(
        '$SYS/' + clientId + '/memory usage',
        memory.rss / BYTE_TO_MBYTE, reason)
    message.qos = 0
    return message
}