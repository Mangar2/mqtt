const testAddon = require('./build/Release/testaddon.node')
console.log('addon', testAddon)

function delay (timeInMilliseconds) {
    return new Promise(resolve => setTimeout(resolve, timeInMilliseconds))
}

const serial = new testAddon.TinySerial()
console.log(serial.open('COM5'))
console.log(serial.setOptions({ baudRate: 1000000 }))
for (let index = 0; index < 10; index++) {
    console.log(serial.write('Hello World ' + index))
    delay(2)
}
console.log(serial.close())

module.exports = testAddon
