import { KeyValueStore } from '../keyvaluestore.js'


const keyvaluestore = new KeyValueStore({
    port: '9100',
    directory: '.'
})

function runserver () {
    keyvaluestore.run()
}

runserver()