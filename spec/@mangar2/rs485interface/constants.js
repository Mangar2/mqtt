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

module.exports = {
    /**
     * Prefixes for setting switch values. The lower byte contains the bits to set/unset,
     * the higher byte value contains the command (set/unset)
     * @private
     */
    SWITCH_ON: 0x4000,
    SWITCH_OFF: 0x2000,
    /**
     * Command signaling a token ('!')
     * @private
     */
    TOKEN_COMMAND: '!',
    /**
     * Amount of bytes used to transport the message (version 0 and version 1)
     * @private
     */
    MESSAGE_SIZE_V0: 7,
    MESSAGE_SIZE_V1: 9,
    /**
     * Maximum supported address value
     * @private
     */
    MAX_ADDRESS: 127,
    /**
     * Address used to broadcast messages to all receipients
     * @private
     */
    BROADCAST_ADDRESS: 0,
    /**
     * State Information
     * @private
     */
    STATE_UNKNOWN: 0,
    STATE_REBOOT: 1,
    STATE_SINGLE: 2,
    STATE_UNREGISTERED: 3,
    STATE_REGISTERED: 4,
    /**
     * State change result information
     * @private
     */
    STATE_UNCHANGED: 0,
    ENABLE_SEND: 1,
    REGISTRATION_INFO: 2,
    REGISTRATION_REQUEST: 3,
    STATE_CHANGED: 4
}
