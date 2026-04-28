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

const { Callbacks, errorLog } = require('@mangar2/utils')
const { SerialPort } = require('serialport')

/**
 * Called when at least one byte has been read from the serial connection
 * @callback Data
 */

/**
 * Class shielding the serial port class and provides async interfaces
 * The functions open, close, and send data are async functions.
 * Read data by registering the callback 'data' with the on function
 */
class SerialConnection {
    constructor () {
        this._callbacks = new Callbacks(['data'])
        // Serial port
        this._serialPort = 0
    }

    /**
     * Lists all available ports to console
     */
    async listAvailablePorts () {
        console.log('Available ports: ')
        // list serial ports:
        try {
            const ports = await SerialPort.list()
            for (const port of ports) {
                console.log(port.path)
            }
        } catch (err) {
            errorLog(err)
        }
    }

    /**
     * Sets a callback.
     * @param {string} event event name (not case sensitive) for the callback (supports 'data')
     * @param {Data} callback function(...parameter)
     * @throws {Error} if the event is not supported
     * @throws {Error} if the callback is not 'function'
     */
    on (event, callback) {
        this._callbacks.on(event, callback)
    }

    /**
     * Closes the serial port
     */
    async close () {
        return new Promise((resolve, reject) => {
            this._serialPort.close(err => {
                if (!err) {
                    resolve()
                } else {
                    reject(err)
                }
            })
        })
    }

    /**
     * Opens the serial port
     * @param {string} serialPortName name of the serial port (example "COM1")
     * @param {number} baudRate baud rate (example 9600)
     * @returns {Promise}
     */
    async open (serialPortName, baudRate) {
        return new Promise((resolve, reject) => {
            const param = { path: serialPortName, baudRate }
            this._serialPort = new SerialPort(param , err => {
                if (err) {
                    reject(err)
                } else {
                    this._serialPort.on('open', () => {
                        console.log('Serial Port ' + serialPortName + ' opened')
                    })
                    this._serialPort.on('close', function () {
                        console.log('Serial port closed')
                    })
                    this._serialPort.on('error', function (err) {
                        errorLog('Serial port error ' + err)
                    })
                    this._serialPort.on('data', (stream) => {
                        this._callbacks.invokeCallback('data', stream)
                    })
                    resolve()
                }
            })
        })
    }

    /**
     * Sends data to the serial port
     * @param {byte[]} byteArray serial data as byte stream
     * @returns true, if sending was successful
     */
    async sendData (byteArray) {
        return new Promise((resolve, reject) => {
            this._serialPort.write(byteArray, err => {
                if (err) {
                    reject(err)
                } else {
                    resolve()
                }
            })
        })
    }
}

module.exports = SerialConnection
