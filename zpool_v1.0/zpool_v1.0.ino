/**
 * @brief Publication zigbee  de la temperature (DS18B20) de la pression (AN) du pH et ORP (mV) avec calibration
 */

#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee coordinator/router device mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include "OneWireESP32.h" //esp32-ds18b20 par junkfix
#include <ADS1X15.h> //ADS1X15 par Rob Tillaart
#include <Preferences.h>
#include "RunningAverage.h"

/* Zigbee temperature sensor configuration */
#define TEMP_SENSOR_ENDPOINT_NUMBER 10
#define PRESSURE_SENSOR_ENDPOINT_NUMBER 11
/* Zigbee analog device configuration */
#define ANALOG_DEVICE_PH_ENDPOINT_NUMBER 9
#define ANALOG_DEVICE_ORP_ENDPOINT_NUMBER 12
/* Zigbee switch pour calibration */
#define ZIGBEE_PH401_ENDPOINT 4
#define ZIGBEE_PH700_ENDPOINT 5
#define ZIGBEE_ORP256_ENDPOINT 6

float readTemp();
uint16_t readPressure();
float readPHmV();
float readORPmV();
float readPH();
void setPH4(bool);
void setPH7(bool);
void setORP(bool);
float mVtopH(float,float);
static void sensors_values_update(void);

uint8_t analogPin = A0;
uint8_t button = BOOT_PIN;
OneWire32 ds(1); 
ADS1115 ADS(0x48);
Preferences calib;

struct calib {
    float ph;
    float mv;
    float t;
};

struct calib ph401;
struct calib ph700;
struct calib orp256;

float alpha;
float pression_max; //pression pour 3300mV

uint64_t addr[2];
unsigned long lastReportTime = 0;
RunningAverage mVmoyen(100);

ZigbeeAnalog zbAnalogPH = ZigbeeAnalog(ANALOG_DEVICE_PH_ENDPOINT_NUMBER);
ZigbeeTempSensor zbTempSensor = ZigbeeTempSensor(TEMP_SENSOR_ENDPOINT_NUMBER);
ZigbeePressureSensor zbPressureSensor = ZigbeePressureSensor(PRESSURE_SENSOR_ENDPOINT_NUMBER);
ZigbeeAnalog zbAnalogORP = ZigbeeAnalog(ANALOG_DEVICE_ORP_ENDPOINT_NUMBER);

ZigbeeBinary zbBinaryPH4 = ZigbeeBinary(ZIGBEE_PH401_ENDPOINT);
ZigbeeBinary zbBinaryPH7 = ZigbeeBinary(ZIGBEE_PH700_ENDPOINT);
ZigbeeBinary zbBinaryORP = ZigbeeBinary(ZIGBEE_ORP256_ENDPOINT);

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");
  
  // Pour trouver l'adresse du DS18B20
  uint8_t devices = ds.search(addr, 2);
  for (uint8_t i = 0; i < devices; i += 1) {
     Serial.printf("%d: 0x%llx,\n", i, addr[i]);
  }

  // Resolution du CAN à 10 bits
  analogReadResolution(10);
  //ADS1115
  Wire.begin();
  ADS.begin();
  ADS.setGain(0);

  // Init button switch
  pinMode(button, INPUT_PULLUP);

  // Option: nom de l'appareil et du modèle Zigbee
  zbTempSensor.setManufacturerAndModel("ZPool", "ZigbeePoolSensorsCalib");

  // Définition les valeurs minimale et maximale de mesure de la température
  zbTempSensor.setMinMaxValue(-10, 50);

  // Définition les valeurs minimale et maximale de mesure de la pression (hPa)
  zbPressureSensor.setMinMaxValue(0, 10000);

  // Option : Tolerance pour la mesure de temperatureen °C (mini 0.01°C)
  zbTempSensor.setTolerance(0.1);

  // Option: Tolerance pour la mesure de pression (hPa)
  zbPressureSensor.setTolerance(1);

  // Analog input : pH
  zbAnalogPH.addAnalogInput();
  zbAnalogPH.setAnalogInputApplication(ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
  zbAnalogPH.setAnalogInputDescription("pH");
  zbAnalogPH.setAnalogInputResolution(0.01);

  // Analog input : ORP
  zbAnalogORP.addAnalogInput();
  zbAnalogORP.setAnalogInputApplication(ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
  zbAnalogORP.setAnalogInputDescription("ORP");
  zbAnalogORP.setAnalogInputResolution(0.01);

  // Analog output : Calibration du pH et ORP
  zbBinaryPH4.addBinaryOutput();
  zbBinaryPH4.setBinaryOutputApplication(BINARY_OUTPUT_APPLICATION_TYPE_HVAC_OTHER);
  zbBinaryPH4.setBinaryOutputDescription("PH401 Calibration");

  zbBinaryPH7.addBinaryOutput();
  zbBinaryPH7.setBinaryOutputApplication(BINARY_OUTPUT_APPLICATION_TYPE_HVAC_OTHER);
  zbBinaryPH7.setBinaryOutputDescription("PH700 Calibration");

  zbBinaryORP.addBinaryOutput();
  zbBinaryORP.setBinaryOutputApplication(BINARY_OUTPUT_APPLICATION_TYPE_HVAC_OTHER);
  zbBinaryORP.setBinaryOutputDescription("ORP256 Calibration");

  zbBinaryPH4.onBinaryOutputChange(setPH4);
  zbBinaryPH7.onBinaryOutputChange(setPH7);
  zbBinaryORP.onBinaryOutputChange(setORP);

  // Ajout des endpoints Zigbee
  Zigbee.addEndpoint(&zbTempSensor);
  Zigbee.addEndpoint(&zbPressureSensor);
  Zigbee.addEndpoint(&zbAnalogPH);
  Zigbee.addEndpoint(&zbAnalogORP);
  Zigbee.addEndpoint(&zbBinaryPH4);
  Zigbee.addEndpoint(&zbBinaryPH7);
  Zigbee.addEndpoint(&zbBinaryORP);


  Serial.println("Demarrage Zigbee...");
  // Une fois tous les EP enregistrés, démarrez Zigbee en mode Routeur.
  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    Serial.println("Echec du démarrage Zigbee !");
    Serial.println("Reboot...");
    ESP.restart();
  } else {
    Serial.println("Zigbee démarré avec succés !");
  }
  Serial.println("Connexion au réseau ");
  while (!Zigbee.connected()) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("Connecté");
  
  // Calibration
  calib.begin("calibration", false); 
  ph401.ph = calib.getFloat("ph401",4.01);
  ph401.mv = calib.getFloat("v401",-2857.3);
  ph401.t = calib.getFloat("t401",20);
  ph700.ph = calib.getFloat("ph700",7.00);
  ph700.mv = calib.getFloat("v700",0);
  ph700.t = calib.getFloat("t700",20);
  orp256.ph = calib.getFloat("orp",256);
  orp256.mv = calib.getFloat("orp256",256);
  orp256.t = calib.getFloat("t256",20);
  alpha = calib.getFloat("alpha",0);
  pression_max = calib.getFloat("pression_max",2068);
  
  Serial.printf("Calibration : tension pH 4.01 = %f mV\r\n", ph401.mv);
  Serial.printf("Calibration : tension pH 7 = %f mV\r\n", ph700.mv);
  Serial.printf("Calibration : tension ORP 256mV = %f mV\r\n", orp256.mv);
  Serial.printf("Coeff delta_pH = %f ph/K\r\n", alpha);
  Serial.printf("Pression max = %f hPa\r\n", pression_max);

  // Lancement de la fonction de Maj des capteurs
  xTaskCreate(sensors_values_update, "sensors_update", 2048, NULL, 10, NULL);

  // Set reporting interval for temperature measurement in seconds, must be called after Zigbee.begin()
  // min_interval and max_interval in seconds, delta (temp change in 0,1 °C)
  // if min = 1 and max = 0, reporting is sent only when temperature changes by delta
  // if min = 0 and max = 10, reporting is sent every 10 seconds or temperature changes by delta
  // if min = 0, max = 10 and delta = 0, reporting is sent every 10 seconds regardless of temperature change
  zbTempSensor.setReporting(0, 60, 1);
  zbPressureSensor.setReporting(0, 60, 10);
  zbAnalogPH.setAnalogInputReporting(0, 60, 0.1);
  zbAnalogORP.setAnalogInputReporting(0, 60, 10);
}

void loop() {
  // verification du bouton boot pour faire un factory reset
  if (digitalRead(button) == LOW) {  // Bouton pressé
    // Anti-rebond
    delay(100);
    int startTime = millis();
    while (digitalRead(button) == LOW) {
      delay(50);
      if ((millis() - startTime) > 3000) {
        // Si le bouton est pressé plus de 3s, faire un factory reset du Zigbee et reboot
        Serial.println("Reset du Zigbee aux valeurs d'usine et reboot dans 1s.");
        delay(1000);
        Zigbee.factoryReset();
      }
    }
  }
  delay(100);
}

float readTemp()
{
  ds.request();
  delay(10);
  //lecture temperature DS18B20
  float temperature;
  ds.getTemp(addr[0], temperature);
  Serial.printf("Maj de la temperature : %.2f°C\r\n", temperature);
  return temperature;
}

uint16_t readPressure(){
  uint32_t pressure_mv = analogReadMilliVolts(A0);
  uint16_t pression = (uint16_t)((float)pressure_mv/3300*pression_max);//30Psi = 2068hPa
  Serial.printf("Maj de la pression : %d hPa (%d mV)\r\n", pression, pressure_mv);
  return pression;
}

float readPHmV(){
    //lecture ADS
    int16_t val_01 = ADS.readADC_Differential_0_1();  // Lecture différentielle entre AIN0 et AIN1
    float mvolts_01 = ADS.toVoltage(val_01)*1000.0; 
    return mvolts_01;
}

float readORPmV(){
    //lecture ADS
    int16_t val_23 = ADS.readADC_Differential_2_3();  // Lecture différentielle entre AIN2 et AIN3
    float mvolts_23 = ADS.toVoltage(val_23)*1000.0; 
    return mvolts_23;
}

float readPH(){
    float mvolts_01 = readPHmV(); 
    float ph = mVtopH(mvolts_01,  readTemp());
    Serial.printf("Maj du pH: %f\r\n", ph);
    return ph;
}

float readORP(){
    float mvolts_23 = readORPmV();
    float mv256 = mvolts_23 + (orp256.ph-orp256.mv);
    Serial.printf("Maj de la tension ORP : %f mV\r\n", mv256);
    return mv256;
}

/************************* Calibration ******************************/
void setPH4(bool value) {
  Serial.printf("Calibration PH 4.01 : %d\r\n", value);
  if (value)
  {
    for (int i=0; i<100;i++)
    {
      float mv = readPHmV();
      mVmoyen.addValue(mv);
      delay(100);
    }
    ph401.mv = mVmoyen.getAverage();
    ph401.t = readTemp();
    mVmoyen.clear();
    calib.putFloat("v401", ph401.mv);
    calib.putFloat("t401", ph401.t);
    Serial.printf("solution PH = 4.01 : %f mV\r\n", ph401.mv);
    zbBinaryPH4.setBinaryOutput(false);
    zbBinaryPH4.reportBinaryOutput();
  }
}

void setPH7(bool value) {
  Serial.printf("Calibration PH 7.00 : %d\r\n", value);
  if (value)
  {
    for (int i=0; i<100;i++)
    {
      float mv = readPHmV();
      mVmoyen.addValue(mv);
      delay(100);
    }
    ph700.mv = mVmoyen.getAverage();
    ph700.t = readTemp();
    mVmoyen.clear();
    calib.putFloat("v700", ph700.mv);
    calib.putFloat("t700", ph700.t);
    Serial.printf("solution PH = 7.00 : %f mV\r\n", ph700.mv);
    zbBinaryPH7.setBinaryOutput(false);
    zbBinaryPH7.reportBinaryOutput();
  }
}

void setORP(bool value) {
  Serial.printf("Calibration ORP : %d\r\n", value);
  if (value)
  {
    for (int i=0; i<100;i++)
    {
      float mv = readORPmV();
      mVmoyen.addValue(mv);
      delay(100);
    }
    orp256.mv = mVmoyen.getAverage();
    mVmoyen.clear();
    calib.putFloat("orp256", orp256.mv);
    Serial.printf("solution ORP = 256mV : %f mV\r\n", orp256.mv);
    zbBinaryORP.setBinaryOutput(false);
    zbBinaryORP.reportBinaryOutput();
  }
}

float mVtopH(float mv, float T=20){
  float a = (ph700.ph-ph401.ph)/(ph700.mv-ph401.mv);
  float b = ph700.ph-a*ph700.mv;
  float dT = T-ph700.t;
  float ph = (a+alpha*dT)*mv +b;
  return ph;
}

/************************ Maj capteurs *****************************/
static void sensors_values_update(void *arg) {
  for (;;) {
    float temp = readTemp();
    uint16_t pression = readPressure();
    float ph = readPH();
    float orp = readORP();
    zbTempSensor.setTemperature(temp);
    zbPressureSensor.setPressure(pression);
    zbTempSensor.reportTemperature();
    zbPressureSensor.report();
    zbAnalogPH.setAnalogInput(ph);
    zbAnalogORP.setAnalogInput(orp);
    delay(10000);
  }
}