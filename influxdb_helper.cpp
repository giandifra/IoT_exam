#include "influxdb_helper.h"
#include "Arduino.h"

/* un'istanza di InlfuxDbClient */
InfluxDBClient influxDBClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN,
                      InfluxDbCloud2CACert);

/* definisco la misura cioè la tabella in cui inserire i dati */
Point sensorsTable(_measurement);

void setupSensorsTag() {

  /* preparo la connessione su influxDB */
  /* aggiiungo i tag nel Point per salvare in modo ordinato i dati sul server */
  sensorsTable.addTag("host", _host);
  sensorsTable.addTag("location", _location);
  sensorsTable.addTag("room", _room);
}

/* funzione per scrivere su influxDB */
void writeToInfluxDB(float t1, float t2, int relay_status) {
  sensorsTable.clearFields();
  sensorsTable.addField("t_hot_zone", t1);
  sensorsTable.addField("t_cold_zone", t2);
  sensorsTable.addField("heating_mat_status", relay_status);
  Serial.print("Writing: ");
  Serial.println(sensorsTable.toLineProtocol());

  /* ora scriviamo sul server */
  if (!influxDBClient.writePoint(sensorsTable)) {
    Serial.print("InfluxDB write failed ");
    Serial.println(influxDBClient.getLastErrorMessage());
  }
}
