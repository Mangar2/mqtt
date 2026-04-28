import { KeyValueStore } from '../keyvaluestore.js'
import { HttpClient } from '@mangar2/httpservice'
import { delay } from '@mangar2/utils'
import UnitTest from '@mangar2/unittest'

import { fileURLToPath } from 'url'
import { dirname } from 'path'

const __filename = fileURLToPath(import.meta.url)
const __dirname = dirname(__filename)

const unitTest = new UnitTest()


const keyvaluestore = new KeyValueStore({
    port: '9100',
    directory: __dirname
})

function runserver () {
    keyvaluestore.run()
}

async function storeFile () {
    const client = new HttpClient('localhost', '9100')
    let httpResult = await client.post({ path: '/file/test', payload: 'Hello World' } )
    unitTest.assertEqual(httpResult.statusCode, 200)
    httpResult = await client.post({ 
        path: '/file/test/json', 
        payload: JSON.stringify('Hello World'), 
        headers: { 'Content-Type':'Application/JSON'}} 
    )
    unitTest.assertEqual(httpResult.statusCode, 200)
    httpResult = await client.getRequest('/file/test')
    unitTest.assertEqual(httpResult.statusCode, 200)
    unitTest.assertEqual(httpResult.headers['content-type'], 'Application/Json')
    unitTest.assertEqual(httpResult.payload, '"Hello World"')
    httpResult = await client.getRequest('/file/test/json')
    unitTest.assertEqual(httpResult.statusCode, 200)
    unitTest.assertEqual(httpResult.headers['content-type'], 'Application/Json')
    unitTest.assertEqual(httpResult.payload, '"Hello World"')
}

async function run() {
    runserver()
    await delay(100)
    await storeFile()
    keyvaluestore.close()
    console.log('Test finished')
    unitTest.showResult(8)
}

run()

