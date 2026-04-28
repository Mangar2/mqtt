/**
 * @license
 * This software is licensed under the GNU LESSER GENERAL PUBLIC LICENSE Version 3. It is furnished
 * "as is", without any support, and with no warranty, express or implied, as to its usefulness for
 * any purpose.
 *
 * @author Volker Böhm
 * @copyright Copyright (c) 2020 Volker Böhm
 * @brief Class providing a key/value store.
 * It stores one file per key
 */

'use strict'

import { shutdown } from '@mangar2/utils'
import { HttpServer } from '@mangar2/httpservice'
import Persist from '@mangar2/persist'
import { sanitizeConfiguration } from './configuration.js'
import errorLog from '@mangar2/errorlog'

const DEBUG = true

function pathToFilename(path) {
    let result = ''
    for (let i = 0; i < path.length; i++) {
        result += path.charCodeAt(i)
    }
    return result
}

export class KeyValueStore {

    constructor(configuration) {
        this._configuration = sanitizeConfiguration(configuration)
        this._server = new HttpServer(this._configuration.port)
        this._persist = new Persist({ keepFiles: this._configuration.keepFiles })
    }

    get port() {
        return this._server.address ? this._server.address.port : undefined
    }

    /**
     * Handles a post request
     * @param {string} payload object payload
     * @param {string} path path of the request
     * @param {object} res result object
     */
    onOptions(res) {
        res.writeHead(200, { 
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'POST, GET, OPTIONS',
            'Access-Control-Allow-Headers': 'content-type'
        })
        res.end('')
    }

    /**
     * Writes a file to the key value store
     * @param {string} path path used to identify the file
     * @param {any} object object to store (it will be stringified)
     */
    async writeFile(path, object) {
        const filename = pathToFilename(path)
        const directory = this._configuration.directory
        await this._persist.saveObjectToFile(directory, filename, object)
        console.log(`${new Date().toLocaleString()} File ${directory}/${filename} written`)
    }

    /**
     * Reads a file from the key value store
     * @param {string} path path used to identify the file
     * @returns {any} object read
     */
    async readFile(path) {
        const filename = pathToFilename(path)
        const directory = this._configuration.directory
        const payload = await this._persist.readData(directory, filename)
        console.log(`${new Date().toLocaleString()} File ${directory}/${filename} read`)
        return payload
    }

    /**
     * Handles a post request
     * @param {string} payload object payload
     * @param {object} headers request headers
     * @param {string} path path of the request
     * @param {object} res result object
     */
    async onPost(payload, headers, path, res) {
        try {
            if (path.length > 100) {
                res.writeHead(400,  { 'Content-Type': 'text/plain' })
                res.end('Error: Key too long, a maximum of 100 characters are supported')
            } else {
                const contentType = headers['content-type']
                const isJson = contentType && contentType.toLowerCase() === 'application/json'
                const parsedData = isJson ? JSON.parse(payload) : payload
                await this.writeFile(path, parsedData)
                res.writeHead(200,  { 'Access-Control-Allow-Origin': '*' })
                res.end('')
            }
        }
        catch (err) {
            errorLog(err, DEBUG)
            res.writeHead(400,  { 'Content-Type': 'text/plain' })
            res.end('Error in request')
        }
    }

    /**
     * Handles a get request
     * @param {string} path get request path
     * @param {object} res get request result object
     */
    async onGet(path, res) {
        try {
            if (path.length > 100) {
                res.writeHead(400,  { 'Content-Type': 'text/plain' })
                res.end('Error: Key too long, a maximum of 100 characters are suported')
            } else {
                const payload = await this.readFile(path)
                res.writeHead(200,  { 
                    'Access-Control-Allow-Origin': '*', 
                    'Content-Type': 'Application/Json' 
                })
                res.end(JSON.stringify(payload))
            }
        } catch (err) {
            errorLog(err, DEBUG)
            res.writeHead(400,  { 'Content-Type': 'text/plain' })
            res.end('Error in request')
        }
    }

    /**
     * Closes the server, stops listening
     */
    async close() {
        this.running = false
        await this._server.close()
    }


    /**
     * Starts the server
     */
    run() {
        shutdown(async () => {
            await this._server.close()
            process.exit(0)
        })
        this._server.on('POST', async (payload, headers, path, res) => { this.onPost(payload, headers, path, res) })
        this._server.on('GET', async (searchParams, headers, path, res) => { this.onGet(path, res) })
        this._server.on('OPTIONS', (payload, headers, path, res) => { this.onOptions(res) })
        this._server.listen()
    }
}
