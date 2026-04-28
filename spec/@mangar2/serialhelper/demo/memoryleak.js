// 18:00, 12MB
/**
 * Arduino Code
void setup() {  Serial.begin(1000000); }
void loop() { if (Serial.available()) Serial.write(Serial.read()); }
45,7 MB after 25.000.000 Write
}
*/

var SerialPort = require('serialport')

function delay (timeInMilliseconds) {
    return new Promise(resolve => setTimeout(resolve, timeInMilliseconds))
}

function printMemoryUsage (sendCount) {
    const memory = process.memoryUsage()
    console.log('rss:%o, ht:%o, hu:%o, ex:%o, cnt:%o',
        memory.rss / 1000000, memory.heapTotal / 1000000, memory.heapUsed / 1000000, memory.external / 1000000, sendCount)
}

(async () => {
    while (true) {
        const serialPort = new SerialPort('COM5', { baudRate: 1000000 }, err => { if (err) console.error(err) })
        serialPort.on('data', stream => {})
        serialPort.on('error', err => { if (err) console.error(err) })
        await delay(1000)
        for (let sendCount = 0; sendCount < 10000000; sendCount++) {
            if (!serialPort.write('Hello World, memory leak? ' + sendCount)) console.log('write error')
            if (sendCount % 10000 === 0) printMemoryUsage(sendCount)
            await delay(2)
        }
        serialPort.close()
        printMemoryUsage(10000000)
        await delay(5000)
    }
})()
