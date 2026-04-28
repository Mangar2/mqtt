/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @overview
 * Stores messages and checks, if a new message (usually a reply) matches one of the send messages
 * It only matches "action messages", i.e. messages whose topic ends on '/set', '/get', '/temporary', or '/blink'
 */

import { Types } from '@mangar2/utils';
import { Message } from './message';

const ActionList = ['set', 'get', 'temporary', 'blink'];

/**
 * Matches incoming "set" messages and outgoing messages to fill up the reason info
 * @example
 * const match = new MatchMessages();
 * match.addReceivedMessage(new Message('a/b', 'on', 'just a reason'));
 * const replyMessage = new Message('a/b', 'on', 'reply');
 * replyMessage = match.matchAndUpdateReplyMessage(replyMessage);
 */
export class MatchMessages {

  private _messageList: Record<string, Message>;

  /**
   * Constructs a new MatchMessages instance
   */
  constructor() {
    this._messageList = {};
  }

  /**
   * Adds an action message to the received message list to match it with a reply
   * @param message - The message to store in the "received message list"
   */
  addReceivedMessage(message: Message): void {
    const topic = message.topic;
    const topicArray = topic.split('/');
    const lastPartOfTopic = topicArray.pop();

    if (lastPartOfTopic && ActionList.includes(lastPartOfTopic)) {
      const replyTopic = topicArray.join('/');
      this._messageList[replyTopic] = message;
    }
  }

  /**
   * Checks, if two values are matching
   * @param value1 - First value to match
   * @param value2 - Second value to match
   * @returns Returns true if the values match, otherwise false
   */
  private _isValueMatching(value1: string | number, value2: string | number): boolean {
    const identical = value1 === value2;
    const sameNumber = !isNaN(value1 as number) && Number(value1) === Number(value2);

    return identical || sameNumber;
  }

  /**
   * Checks, if a reply message matches any message received lately (in the last `maxTimespan` seconds)
   * @param message - The message to match
   * @param maxTimespan - Maximum timespan in milliseconds to look for matching messages. Defaults to 30000ms (30s)
   * @returns Returns true if the message has a matching message with the same topic and value, otherwise false
   */
  private getMatchingMessage(message: Message, maxTimespan = 30000): Message | null {
    let result = null;
    const { topic, reason } = message;
    const matchingMessage = this._messageList[topic]

    if (matchingMessage) {
      const matchingReason = matchingMessage.reason;
      const valueMatches = this._isValueMatching(matchingMessage.value, message.value);

      if (valueMatches && Array.isArray(matchingReason) && Array.isArray(reason)) {
        const matchingTimestamp = new Date(matchingReason[0].timestamp).getTime();
        const replyTimestamp = new Date(reason[0].timestamp).getTime();
        const timeBetweenReplyAndReceive = replyTimestamp - matchingTimestamp;

        if (timeBetweenReplyAndReceive >= 0 && timeBetweenReplyAndReceive <= maxTimespan) {
          result = matchingMessage
        };
      }
    }

    return result;
  }

  /**
   * Matches a reply message with a received message, updates the reasons and removes
   * the received message from the message list.
   *
   * The messages match if they have the same core topic (the topic without action, 'set', 'get', 'temporary', 'blink') and the same value.
   *
   * @param message - The reply message to match with a received message
   * @returns The matched message with updated reason or unchanged message if the message does not match with a stored message
   */
  matchAndUpdateReplyMessage(message: Message): Message {
    const { topic, reason } = message;
    const matchingMessage = this.getMatchingMessage(message)

    if (matchingMessage) {
      const matchingReason = matchingMessage.reason;
      if (Types.isArray(matchingReason)) {
        if (!Types.isArray(reason)) {
          message.reason = [...matchingReason]
        } else {
          message.reason = [...matchingReason, ...reason];
        }
      }
    }

    // We delete the incoming message with the same topic even if the message values are not matching to ensure that 
    // old (outdated) messages are not matched. Usually this should not happen ... but we cannot ensure
    if (this._messageList[topic]) {
      delete this._messageList[topic];
    }

    return message;
  }
}
