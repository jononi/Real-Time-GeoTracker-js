const FieldType = require('influx').FieldType

module.exports = {
    "particle": {
      "username": "particle_username",
      "password": "particle_password",
      "deviceName": "electron_device_name",
      "eventName": "event" //'event' will capture all events, regardless of their name
    },
    // InfluxDB connection configuration
    "influx": {
      "host": "localhost",
      "port":	8086,
      "username": "influxdb_username",
    	"password": "influxdb_password",
    	"database": "Tracking",
      "schema" : [ {
        measurement : "location",
        tags: ['session_id'],
        fields: {
          'latitude': FieldType.FLOAT,
          'longitude': FieldType.FLOAT,
          'geohash': FieldType.STRING,
          'elevation': FieldType.FLOAT,
          'speed': FieldType.FLOAT
        }
      }, {
        measurement : "battery",
        tags: ['session_id'],
        fields: {
          'percentage': FieldType.FLOAT,
        }
      }]
  }
}
