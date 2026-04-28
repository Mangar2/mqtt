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
     * Maximum supported address value
     * @private
     */
    MAX_ADDRESS: 127
}
