/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 */

import { Types } from '@mangar2/utils'

/**
 * Class representing a tree structure for managing patterns. Patterns are strings with '/' as separators.
 */
export class PatternTree {
    private _tree: { [key: string]: any } = {};

    /**
     * Adds a pattern to the pattern tree. The pattern uses '/' as separator.
     * @param {string} pattern - The pattern string with '/' as separator.
     * @param {number} [value=0] - The value associated with the pattern. Default is 0.
     */
    addPattern (pattern: string, value: number = 0): void {
        const patternChunks = pattern.split('/')
        let node = this._tree
        for (const chunk of patternChunks) {
            if (!node[chunk]) {
                node[chunk] = {}
            }
            node = node[chunk]
        }
        node[0] = value
    }

    /**
     * Recursively removes a pattern (exact match) from the tree
     * @param {string[]} topicChunks - The chunks constituting the topic.
     * @param {Object} node - The current node in the pattern tree
     * @param {number} [index=0] - The current index (depth) in the topic array
     * @private
     */
    private _deletePatternRec(topicChunks: string[], node: any, index: number = 0): void {
        const chunk = topicChunks[index]
        if (chunk === undefined) {
            if (node[0] !== undefined) {
                delete node[0]
            }
            return
        }
        const childNode = node[chunk]
        if (childNode !== undefined) {
            this._deletePatternRec(topicChunks, childNode, index + 1)
            if (Object.keys(childNode).length === 0) {
                delete node[chunk]
            }
        }
    }

    /**
     * Recursively searches for a topic in a pattern tree, returns the first match
     * @param {string[]} topicChunks - Array of chunks making up the topic.
     * @param {Object} node - Current node in the pattern tree
     * @param {number} [index=0] - Current index (depth) in the topic array
     * @returns {number|undefined} - The value associated with the first matching pattern, or undefined if no match is found
     * @private
     */
    private _getFirstMatchRec (topicChunks: string[], node: any, index: number = 0): number | undefined {
        const topic = topicChunks[index]
        if (topic === undefined) {
            return node[0]
        }
        const isSystemChunk = (index === 0) && topic.charAt(0) === '$'
        if (!isSystemChunk) {
            if (node['#']) {
                return node['#'][0]
            }
            if (node['+']) {
                const result = this._getFirstMatchRec(topicChunks, node['+'], index + 1)
                if (result !== undefined) return result
            }
        }
        if (node[topic]) {
            const result = this._getFirstMatchRec(topicChunks, node[topic], index + 1)
            if (result !== undefined) return result
        }
        return undefined
    }

    /**
     * Returns the maximum of two elements that are either a number or undefined. Undefined values are ignored.
     * If either of the elements is undefined, the other element is returned.
     * @param {function(a: number, b: number): boolean} isBetter A callback function that returns `true` if the first element is better than the second element.
     * @param {number|undefined} a The first element to compare.
     * @param {number|undefined} b The second element to compare.
     * @private
     * @returns {number|undefined} The maximum element or undefined.
     */
    private _max (isBetter: (a: number, b: number) => boolean, a: number | undefined, b: number | undefined): number | undefined {
        if (a === undefined) {
            return b
        }
        if (b === undefined) {
            return a
        }
        return isBetter(a, b) ? a : b
    }

    /**
     * Recursively searches for a topic in a pattern tree, returns the best match (match with highest value)
     * @param {function(a: number, b: number): boolean} isBetter Returns true if a is better than b.
     * @param {string[]} topicChunks Topic chunks to search for.
     * @param {Object} node Current node in the pattern tree.
     * @param {number} [index = 0] Current index (depth) in the topic array.
     * @private
     * @returns {number|undefined} The value of the best match or undefined if no match is found.
     */
    private _getBestMatchRec (isBetter: (a: number, b: number) => boolean, topicChunks: string[], node: any, index: number = 0): number | undefined {
        const topic = topicChunks[index]
        if (topic === undefined) {
            return node[0]
        }
        const isSystemChunk = (index === 0) && topic.charAt(0) === '$'
        let bestMatch: number | undefined
        if (!isSystemChunk) {
            if (node['#']) {
                bestMatch = this._max(isBetter, bestMatch, node['#'][0])
            }
            if (node['+']) {
                const cur = this._getBestMatchRec(isBetter, topicChunks, node['+'], index + 1)
                bestMatch = this._max(isBetter, bestMatch, cur)
            }
        }
        if (node[topic]) {
            const cur = this._getBestMatchRec(isBetter, topicChunks, node[topic], index + 1)
            bestMatch = this._max(isBetter, bestMatch, cur)
        }
        return bestMatch
    }

    /**
     * Searches for the first match of a topic in the pattern tree.
     * @param {string} topic The topic to search for.
     * @returns {number|undefined} The value associated with the first matching pattern in the tree,
     *   or `undefined` if no matching pattern is found.
     */
    getFirstMatch (topic: string): number | undefined {
        const topicChunks = topic.split('/')
        return this._getFirstMatchRec(topicChunks, this._tree)
    }

    /**
     * Gets the best match of a topic using the specified "isBetter" function.
     * The "isBetter" function should take two arguments and return true if the first argument
     * is better than the second argument.
     * @param {string} topic - The topic to search for.
     * @param {(a: number, b: number) => boolean} isBetter - The function used to compare two values.
     * @returns {number|undefined} The best match found in the pattern tree, or undefined if no match was found.
     * @throws {TypeError} Thrown if the "isBetter" argument is not a function.
     */
    getBestMatch (topic: string, isBetter: (a: number, b: number) => boolean): number | undefined {
        if (!Types.isFunction(isBetter)) {
            throw new TypeError('The "isBetter" argument must be a function')
        }
        const topicChunks = topic.split('/')
        return this._getBestMatchRec(isBetter, topicChunks, this._tree)
    }

    /**
     * Deletes a pattern from the tree.
     * @param {string} pattern The pattern to delete.
     * @returns {undefined}
     */
    deletePattern (pattern: string): void {
        const topicChunks = pattern.split('/')
        this._deletePatternRec(topicChunks, this._tree)
    }
}


