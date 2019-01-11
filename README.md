This version of the project uses a node.js based server side event listener and is deprectaed.
It's replaced by a more robust and up-to-date version based on [Particle's webhooks](https://docs.particle.io/tutorials/device-cloud/webhooks/) and [Influx telegraf](https://portal.influxdata.com/downloads/) data collector [plugin](https://github.com/influxdata/telegraf/tree/master/plugins/inputs/webhooks/particle) and is [hosted here](https://github.com/jononi/Real_Time_Geo_Tracker). 

Requisites:

Hardware:

* Particle's Electron (tested on 2G version)
* GPS/GNSS unit with Serial output (Tested on NeoM8N GNSS module)

Software:

Libraries for Electron firmware:
* TinyGPS library
* Ubidots (optional)

For logging and dashboard:
* InlfuxDB >= 1.0
* Grafana >= 3.0
* Node.js >= 6.0
* [Particle API JS](https://docs.particle.io/reference/javascript/)
* [Node-Influx](https://node-influx.github.io/)
* [latlon-geohash](https://github.com/chrisveness/latlon-geohash) 
