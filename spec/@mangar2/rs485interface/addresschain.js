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

/**
 * Contains an ordered chain of addresses
 */
class AddressChain {
    constructor () {
        this._chain = []
    }

    /**
     * Inserts an address, version pair to an ordered address chain
     * @param {number} address address of the element to insert
     * @param {number} version version of the element to insert
     */
    insert (address, version) {
        let isIncluded = false
        for (const index in this._chain) {
            if (this._chain[index].address === address) {
                isIncluded = true
                break
            }
            if (this._chain[index].address > address) {
                this._chain.splice(index, 0, { address, version })
                isIncluded = true
                break
            }
        }
        if (!isIncluded) {
            this._chain.push({ address, version })
        }
    }
}

module.exports = AddressChain
