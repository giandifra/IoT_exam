#ifndef INFLUXDB_HELPER_H
#define INFLUXDB_HELPER_H

#include "Arduino.h"
#include <InfluxDbClient.h>   /* per libreria di comunicazione con InfluxDB */
#include <InfluxDbCloud.h>    /* per gestire il token di sicurezza */

#define INFLUXDB_URL    "http://93.186.254.118:8086"
#define INFLUXDB_ORG    "uniurb"
#define INFLUXDB_BUCKET "test"
#define INFLUXDB_TOKEN "7q44Rz0f0IZYM4SYguqyPB5RPafXPEagZUpRuIUBp3aoDT3HVQzFg5c0Hg_RY8Khk8cH8MjuApdyQsKrFyaF4w=="

#define _measurement "monitor_task"
#define host "ESP_GM_Di_Francesco"
#define location "Sant'Egidio alla Vibrata"
#define room "camera"


/* un'istanza di InlfuxDbClient */
extern InfluxDBClient client;

/* definisco la misura cioè la tabella in cui inserire i dati */
extern Point sensorsTable;

void setupSensorsTag();
void writeToInfluxDB(float t1, float t2, bool relay_status);

#endif
