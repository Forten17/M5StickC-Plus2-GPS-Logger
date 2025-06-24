
if (process.argv.length !== 3) {
    console.error("アプリケーション設定ファイルが必要です")
    return
}
  
const fs = require('fs')
const mqtt = require('mqtt');
const mongodb = require('mongodb')

const appConfig = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))

function mqttInit() {
    let client = mqtt.connect(appConfig.mqtt.url, {
        clientId: appConfig.mqtt.clientId + '-subscriber',
        port: appConfig.mqtt.port,
        clean: true,
        connectTimeout: 4000,
        reconectedPeriod: 1000,
    })

    return client
}

async function insertDocument(doc) {
    let client
    try {
        client = await mongodb.MongoClient.connect(appConfig.mongodb.uri)

        const db = client.db(appConfig.mongodb.db)
        console.log("Stored Data:", doc)
        const res = await db
            .collection(appConfig.mongodb.collection)
            .insertOne(doc)

            console.log("Stored Result Data", res)

            return res
    } catch (error) {
        console.error("MongoDB Error!", error)
    } finally {
        client.close()
    }
}

function buildMongoDocument(payload) {
    let now = new Date()

    try {
        let payloadJSON = JSON.parse(payload)

        console.log("Parased Payload:", payloadJSON)

        let mongoDocument = {
        clientId: appConfig.mqtt.clientId,
        geometry: {type: "Point", coordinates: [parseFloat(payloadJSON.lng), parseFloat(payloadJSON.lat), parseFloat(payloadJSON.alt)]},
        gpstime: payloadJSON.gpstime,
        uploadtime: now.toISOString()
    }

    console.log("MongoDB Document:", mongoDocument)
    return mongoDocument

} catch (error) {
    console.error("Error parsing payload:", error)
    return null
}
}


function main() {
    const mqttClient = mqttInit()

    mqttClient.on('connect', function() {
        console.log('Connected to MQTT Broker')
        mqttClient.subscribe('/' + appConfig.mqtt.clientId + '/location' , {qos: 0})
        console.log(`Subscribed! '${appConfig.mqtt.clientId}'`)
    })
    
    mqttClient.on('message', function (topic, message) {
    console.log(message.toString())
    let doc = buildMongoDocument(message)
    insertDocument(doc)
})
}

main()