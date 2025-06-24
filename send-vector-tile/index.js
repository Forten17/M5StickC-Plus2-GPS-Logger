'use strict'

if (process.argv.length !== 3) {
    console.error("アプリケーション設定ファイルが必要です")
    return
}

let self = this
const fs = require('fs')
const express = require('express')
const mongodb = require('mongodb')

const appConfig = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const app = express()

self.db = null

app.get('/', (req, res) => {
    console.log("Root endpoint accessed")
    res.send('OK')
})


app.get('/api/geojson/:z/:x/:y/?', async (req, res) => {
    console.log(req.params)

    res.header("Access-Control-Allow-Origin", "*")
    res.header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept")

    let response = await getVectorTile(req.params.x, req.params.y, req.params.z, res)
    res.send(response)
})

const getVectorTile = async (x, y, z) => {
    console.log(`Fetching vector tile for z: ${z}, x: ${x}, y: ${y}`)

    let minmax = tile2latlng(x, y, z)
    let result = []
    try {
        result = await self.db.collection(appConfig.mongodb.collection)
            .find({
                geometry:
                {
                    $geoWithin:
                    {
                        $geometry:
                        {
                            type: "Polygon",
                            coordinates: [[
                                [minmax.min.lng, minmax.min.lat],
                                [minmax.max.lng, minmax.min.lat],
                                [minmax.max.lng, minmax.max.lat],
                                [minmax.min.lng, minmax.max.lat],
                                [minmax.min.lng, minmax.min.lat]
                            ]]
                        }
                    }
                }
            }).map((doc) => {
                return {
                    type: "Feature",
                    geometry: doc.geometry,
                    properties: { _id: doc._id }
                }
            }).toArray()
            console.log(result)
    } catch (error) {
        console.log(error)
    }

    return { type: "FeatureCollection", features: result }

}



function tile2latlng(x, y, z) {

    const mod = (a, b) => {
        return a * b < 0 ? a % b + b : a % b
    }

    const x2lng = (x, z) => {

        let lng = mod((x / Math.pow(2, z) * 360), 360) - 180
        return (lng != (-180) ? lng : 180)
    }

    const y2lat = (y, z) => {
        let n = Math.PI - 2 * Math.PI * y / Math.pow(2, z)
        return 180 / Math.PI * Math.atan(0.5 * (Math.exp(n) - Math.exp(-n)))
    }

    let minlng = x2lng(x, z)
    let maxlng = x2lng(parseInt(x, 10) + 1, z)

    let minlat = y2lat(parseInt(y, 10) + 1, z)
    let maxlat = y2lat(parseInt(y, 10), z)

    console.log([minlng,minlat,maxlng,maxlat])
    return { min: { lat: minlat, lng: minlng }, max: { lat: maxlat, lng: maxlng } }

}



async function main() {
    const url = appConfig.mongodb.url
    try {
        const client =await mongodb.MongoClient.connect(url)
        self.db = client.db(appConfig.mongodb.db)
        app.listen(appConfig.api.port, () => console.log('Listening on port ' + appConfig.api.port))
        } catch(err) {
            console.log('Failed to connect to MongoDB', err)
        }
}

main()