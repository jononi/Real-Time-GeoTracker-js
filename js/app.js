var Particle = require('particle-api-js');
var Influx = require('influx')
var Geohash = require('latlon-geohash');

var config = require('./config.js');
var particle = new Particle();
var influx = new Influx.InfluxDB(config.influx)



particle.login(config.particle)
.then(
  function(data) {
    token = data.body.access_token
    console.log('Successfully logged in.')
    // console.log(token)
    return particle.getEventStream({ deviceId: config.particle.deviceName, auth: token })
  },
  function (err) {
    console.log('Could not log in:',err.body.error)
    process.exit(1)
  }
)
.then(
  function(stream) {
    console.log(`Listening for events: ${config.particle.eventName} from ${config.particle.deviceName}:`)
    // stream.on(config.particle.eventName, echoEvent)
    stream.on(config.particle.eventName, cloudEventHandler)
  },
  function(err) {
    console.error("Could not start event listener", err.body.error)
    process.exit(1)
  }
)


cloudEventHandler = function(event_d) {
  if (event_d.name == 'GNSS/Bat') {
    // var battery_data = JSON.parse(event_d.data)
    // console.log(`Battery charge: ${battery_data["percentage"]}%`)
    console.log(`Battery charge: ${event_d.data}%`)

    influx.writePoints([
      {
        measurement: 'battery',
        // tags: { session_id:  'test1'},
        fields: { percentage: event_d.data }
      }],
      {
        retentionPolicy: 'location_2y',
        precision: 's'
      })
      /*
      .then(() => {
        console.log(`Wrote to InfluxDB at: ${event_d.published_at}`)
      },
      err => {console.log(err.stack)
      })*/
      .catch(err => {
        console.log(`Error saving data to InfluxDB! ${err.stack}`)
      })
      // console.log('Wrote to InfluxDB')
    }
    else if (event_d.name == 'GNSS/data') {
      var location_data = JSON.parse(event_d.data)
      // console.log(`lat=${location_data["lat"]},lon=${location_data["lon"]},elev=${location_data["alt"]},mph=${location_data["spd"]}`)
      var geohash_val = Geohash.encode(location_data["lat"], location_data["lon"], 9)
      console.log(`location at ${Date.now()} : ${geohash_val}`)

      influx.writePoints([{
        measurement: 'location',
        tags: { session_id: location_data["s_id"] },
        fields: {
          latitude: location_data["lat"],
          longitude: location_data["lon"],
          geohash: geohash_val,
          elevation: location_data["alt"],
          speed: location_data["spd"]
        },
        // timestamp: location_data["time"],
      }],
      {
        retentionPolicy: 'location_2y',
        precision: 's'
      })
      .catch(err => {
        console.log(`Error writing point to InfluxDB: ${err.stack}`)
      })
    }
else {
  console.log(`${event_d.name} : ${event_d.data})`)
}
}
