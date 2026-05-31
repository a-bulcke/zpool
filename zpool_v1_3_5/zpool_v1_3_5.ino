/**
 * @brief Publication zigbee de la temperature (DS18B20), pression (AN), pH et ORP (mV) avec calibration
 * @version 1.3.5
 * @changelog
 *   1.3.5 - Pas de remontée pendant la calibration - timing lecture temperature à 750ms
 *   1.3.3 - Correction thermique par équation de Nernst
 *            mv normalisé à T_cal avant application du polynôme. Point isopotentiel = pH 7.
 *            S(T) = -0.1984 * (273.15 + T)  [mV/pH]
 *   1.3.2 - Correction stack overflow
 *   1.3.1 - Valeurs des solutions de calibration ajustables depuis Z2M/HA (analog output EP13-16)
 *   1.3.0 - Ajout calibration pH 10.01 (ep7), équation polynomiale degré 2, filtre de Kalman pH+ORP
 *   1.2.4 - OTA
 */

#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee coordinator/router device mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include "OneWireESP32.h"   // esp32-ds18b20 par junkfix
#include <ADS1X15.h>        // ADS1X15 par Rob Tillaart
#include <Preferences.h>
#include "RunningAverage.h" // RunningAverage par Rob Tillaart

/* ──────────────────────────────────────────────────────────────
 *  Endpoints Zigbee
 * ────────────────────────────────────────────────────────────── */
// Mesures
#define TEMP_SENSOR_ENDPOINT_NUMBER       10
#define PRESSURE_SENSOR_ENDPOINT_NUMBER   11
#define ANALOG_DEVICE_PH_ENDPOINT_NUMBER   9
#define ANALOG_DEVICE_ORP_ENDPOINT_NUMBER 12

// Déclencheurs de calibration (binary output → écriture Z2M)
#define ZIGBEE_PH401_ENDPOINT   4
#define ZIGBEE_PH700_ENDPOINT   5
#define ZIGBEE_ORP256_ENDPOINT  6
#define ZIGBEE_PH1001_ENDPOINT  7

// Valeurs des solutions (analog output → slider Z2M/HA)
#define ZIGBEE_PH401_VAL_EP    13   // Valeur cible pH point 1  (ex : 4.01 ou 6.86)
#define ZIGBEE_PH700_VAL_EP    14   // Valeur cible pH point 2  (ex : 7.00 ou 6.86)
#define ZIGBEE_PH1001_VAL_EP   15   // Valeur cible pH point 3  (ex : 10.01 ou 9.18)
#define ZIGBEE_ORP256_VAL_EP   16   // Valeur cible ORP          (ex : 256 ou 240 mV)

/* ──────────────────────────────────────────────────────────────
 *  Filtre de Kalman 1D
 * ────────────────────────────────────────────────────────────── */
struct KalmanFilter1D {
    float x;
    float P;
    float Q;
    float R;
    bool  init;

    KalmanFilter1D(float q = 0.001f, float r = 0.1f)
        : x(0), P(1.0f), Q(q), R(r), init(false) {}

    float update(float z) {
        if (!init) { x = z; P = 1.0f; init = true; return x; }
        P      += Q;
        float K = P / (P + R);
        x      += K * (z - x);
        P      *= (1.0f - K);
        return x;
    }

    void reset() { init = false; }
};

/* ──────────────────────────────────────────────────────────────
 *  Prototypes
 * ────────────────────────────────────────────────────────────── */
void     printCalibSummary();
float    readTemp();
uint16_t readPressure();
float    readPHmV();
float    readORPmV();
float    readPH();
float    readORP();
void     setPH4(bool);
void     setPH7(bool);
void     setPH10(bool);
void     setORP(bool);
void     onPH4ValChange(float);
void     onPH7ValChange(float);
void     onPH10ValChange(float);
void     onORPValChange(float);
void     calibration();
float    mVtopH(float mv, float T);
bool     computePolyCoeffs();
static void sensors_values_update(void *);
void     otaStateChange(bool);

/* ──────────────────────────────────────────────────────────────
 *  Matériel
 * ────────────────────────────────────────────────────────────── */
uint8_t analogPression = A0;
uint8_t button    = BOOT_PIN;
OneWire32 ds(1);
ADS1115 ADS(0x48);
Preferences calib;

/* ──────────────────────────────────────────────────────────────
 *  Structures de calibration
 * ────────────────────────────────────────────────────────────── */
struct sonde {
    float ph;       // Valeur pH (ou mV ORP) de la solution — réglable depuis Z2M
    float mv;       // Tension mesurée lors de la calibration
    float t;        // Température lors de la calibration
    bool  cal = false;
};

struct sonde ph401;    // Point 1 : pH ~4
struct sonde ph700;    // Point 2 : pH ~7
struct sonde ph1001;   // Point 3 : pH ~10
struct sonde orp256;   // ORP

/* Coefficients polynôme : pH = poly_a·mV² + poly_b·mV + poly_c */
float poly_a = 0.0f, poly_b = 0.0f, poly_c = 0.0f;
bool  poly_valid = false;

// Constante de Nernst : S(T) = NERNST_K * (273.15 + T°C)  →  mV/pH
// À 25°C : S = -0.1984 * 298.15 = -59.16 mV/pH
static constexpr float NERNST_K = -0.1984f;

float pression_max;

/* Filtres de Kalman */
KalmanFilter1D kfPH (0.001f, 0.05f);
KalmanFilter1D kfORP(0.1f,   3.0f);

uint64_t addr[2];
RunningAverage mVmoyen(100);

/* ──────────────────────────────────────────────────────────────
 *  Endpoints Zigbee
 * ────────────────────────────────────────────────────────────── */
// Mesures
ZigbeeAnalog         zbAnalogPH       = ZigbeeAnalog(ANALOG_DEVICE_PH_ENDPOINT_NUMBER);
ZigbeeTempSensor     zbTempSensor     = ZigbeeTempSensor(TEMP_SENSOR_ENDPOINT_NUMBER);
ZigbeePressureSensor zbPressureSensor = ZigbeePressureSensor(PRESSURE_SENSOR_ENDPOINT_NUMBER);
ZigbeeAnalog         zbAnalogORP      = ZigbeeAnalog(ANALOG_DEVICE_ORP_ENDPOINT_NUMBER);

// Déclencheurs calibration
ZigbeeBinary zbBinaryPH4  = ZigbeeBinary(ZIGBEE_PH401_ENDPOINT);
ZigbeeBinary zbBinaryPH7  = ZigbeeBinary(ZIGBEE_PH700_ENDPOINT);
ZigbeeBinary zbBinaryPH10 = ZigbeeBinary(ZIGBEE_PH1001_ENDPOINT);
ZigbeeBinary zbBinaryORP  = ZigbeeBinary(ZIGBEE_ORP256_ENDPOINT);

// Valeurs des solutions (analog output — slider Z2M/HA)  ← Nouveau
ZigbeeAnalog zbValPH4   = ZigbeeAnalog(ZIGBEE_PH401_VAL_EP);
ZigbeeAnalog zbValPH7   = ZigbeeAnalog(ZIGBEE_PH700_VAL_EP);
ZigbeeAnalog zbValPH10  = ZigbeeAnalog(ZIGBEE_PH1001_VAL_EP);
ZigbeeAnalog zbValORP   = ZigbeeAnalog(ZIGBEE_ORP256_VAL_EP);

volatile bool otaInProgress = false;
volatile bool calibInProgress = false;
volatile bool initialized = false;

/* ══════════════════════════════════════════════════════════════
 *  DIAGNOSTIC — Affichage des paramètres NVS au démarrage
 * ══════════════════════════════════════════════════════════════ */
void printCalibSummary() {
    const char* SEP  = "==================================================";
    const char* SEP2 = "--------------------------------------------------";

    Serial.println();
    Serial.println(SEP);
    Serial.println("       PARAMETRES DE CALIBRATION (NVS)");
    Serial.println(SEP);

    // ── pH : solutions ───────────────────────────────────────────
    Serial.println("  [Solutions pH]");
    Serial.printf ("    Point 1  : sol=%6.3f pH  mv=%+9.2f mV  T=%5.1f deg C\r\n",
                   ph401.ph,  ph401.mv,  ph401.t);
    Serial.printf ("    Point 2  : sol=%6.3f pH  mv=%+9.2f mV  T=%5.1f deg C\r\n",
                   ph700.ph,  ph700.mv,  ph700.t);
    Serial.printf ("    Point 3  : sol=%6.3f pH  mv=%+9.2f mV  T=%5.1f deg C\r\n",
                   ph1001.ph, ph1001.mv, ph1001.t);

    // ── pH : pente de Nernst aux 3 températures de calibration ───
    Serial.println(SEP2);
    Serial.println("  [Pentes de Nernst]");
    Serial.printf ("    S(T_cal1=%5.1f deg C) = %+7.3f mV/pH\r\n",
                   ph401.t,  NERNST_K * (273.15f + ph401.t));
    Serial.printf ("    S(T_cal2=%5.1f deg C) = %+7.3f mV/pH  <- pivot isopotentiel\r\n",
                   ph700.t,  NERNST_K * (273.15f + ph700.t));
    Serial.printf ("    S(T_cal3=%5.1f deg C) = %+7.3f mV/pH\r\n",
                   ph1001.t, NERNST_K * (273.15f + ph1001.t));

    // ── pH : polynôme ────────────────────────────────────────────
    Serial.println(SEP2);
    Serial.println("  [Polynome  pH = a*mV^2 + b*mV + c]");
    if (poly_valid) {
        Serial.printf("    a = %+.8f\r\n", poly_a);
        Serial.printf("    b = %+.6f  mV/pH\r\n", poly_b);
        Serial.printf("    c = %+.4f  pH\r\n",    poly_c);
        float r1 = poly_a*ph401.mv *ph401.mv  + poly_b*ph401.mv  + poly_c - ph401.ph;
        float r2 = poly_a*ph700.mv *ph700.mv  + poly_b*ph700.mv  + poly_c - ph700.ph;
        float r3 = poly_a*ph1001.mv*ph1001.mv + poly_b*ph1001.mv + poly_c - ph1001.ph;
        Serial.printf("    Residus : r1=%+.4f  r2=%+.4f  r3=%+.4f pH\r\n", r1, r2, r3);
        Serial.println("    Statut  : OK (3 points)");
    } else {
        Serial.println("    Statut  : FALLBACK LINEAIRE (< 3 points ou systeme singulier)");
        float a_lin = (ph700.ph - ph401.ph) / (ph700.mv - ph401.mv);
        float b_lin =  ph700.ph - a_lin * ph700.mv;
        Serial.printf("    a_lin = %+.6f mV/pH   b_lin = %+.4f pH\r\n", a_lin, b_lin);
    }

    // ── ORP ──────────────────────────────────────────────────────
    Serial.println(SEP2);
    Serial.println("  [ORP]");
    Serial.printf ("    Solution  : %7.1f mV  (cible)\r\n",  orp256.ph);
    Serial.printf ("    Mesure    : %7.2f mV  (lors de la calibration)\r\n", orp256.mv);
    Serial.printf ("    Offset    : %+7.2f mV\r\n", orp256.ph - orp256.mv);

    // ── Pression ─────────────────────────────────────────────────
    Serial.println(SEP2);
    Serial.println("  [Pression]");
    Serial.printf ("    Pleine echelle : %.0f hPa  (%.2f bar)\r\n",
                   pression_max, pression_max / 1000.0f);

    // ── Kalman ───────────────────────────────────────────────────
    Serial.println(SEP2);
    Serial.println("  [Filtres Kalman]");
    Serial.printf ("    pH   Q=%.4f  R=%.4f\r\n", kfPH.Q,  kfPH.R);
    Serial.printf ("    ORP  Q=%.4f  R=%.4f\r\n", kfORP.Q, kfORP.R);

    // ── Mémoire heap FreeRTOS ────────────────────────────────────
    Serial.println(SEP2);
    Serial.println("  [Systeme]");
    Serial.printf ("    Heap libre    : %lu octets\r\n", (unsigned long)esp_get_free_heap_size());
    Serial.printf ("    Heap min ever : %lu octets\r\n", (unsigned long)esp_get_minimum_free_heap_size());
    Serial.println(SEP);
    Serial.println();
}

/* ══════════════════════════════════════════════════════════════
 *  SETUP
 * ══════════════════════════════════════════════════════════════ */
void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) delay(10);
    delay(200);   // petit délai supplémentaire pour que le terminal soit prêt
    Serial.println("Starting ZPool v1.3.3...");

    uint8_t devices = ds.search(addr, 2);
    for (uint8_t i = 0; i < devices; i++)
        Serial.printf("%d: 0x%llx,\n", i, addr[i]);

    analogReadResolution(10);
    Wire.begin();
    ADS.begin();
    ADS.setGain(0);
    pinMode(button, INPUT_PULLUP);

    /* ── Capteurs (analog input) ─────────────────────────────── */
    zbTempSensor.setManufacturerAndModel("ZPool", "ZPool");
    zbTempSensor.setVersion(135);
    zbTempSensor.setHardwareVersion(135);
    zbTempSensor.addOTAClient(0x01030500, 0x00000000, 0x0001, 0x4142, 0x1011, 223);
    zbTempSensor.onOTAStateChange(otaStateChange);
    zbTempSensor.setMinMaxValue(-10, 50);
    zbTempSensor.setTolerance(0.1);

    zbPressureSensor.setMinMaxValue(0, 10000);
    zbPressureSensor.setTolerance(1);

    zbAnalogPH.addAnalogInput();
    zbAnalogPH.setAnalogInputApplication(ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
    zbAnalogPH.setAnalogInputDescription("pH");
    zbAnalogPH.setAnalogInputResolution(0.01);

    zbAnalogORP.addAnalogInput();
    zbAnalogORP.setAnalogInputApplication(ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
    zbAnalogORP.setAnalogInputDescription("ORP");
    zbAnalogORP.setAnalogInputResolution(0.01);

    /* ── Déclencheurs de calibration (binary output) ─────────── */
    zbBinaryPH4.addBinaryOutput();
    zbBinaryPH4.setBinaryOutputApplication(BINARY_OUTPUT_APPLICATION_TYPE_HVAC_OTHER);
    zbBinaryPH4.setBinaryOutputDescription("PH401 Calibration");
    zbBinaryPH4.onBinaryOutputChange(setPH4);

    zbBinaryPH7.addBinaryOutput();
    zbBinaryPH7.setBinaryOutputApplication(BINARY_OUTPUT_APPLICATION_TYPE_HVAC_OTHER);
    zbBinaryPH7.setBinaryOutputDescription("PH700 Calibration");
    zbBinaryPH7.onBinaryOutputChange(setPH7);

    zbBinaryPH10.addBinaryOutput();
    zbBinaryPH10.setBinaryOutputApplication(BINARY_OUTPUT_APPLICATION_TYPE_HVAC_OTHER);
    zbBinaryPH10.setBinaryOutputDescription("PH1001 Calibration");
    zbBinaryPH10.onBinaryOutputChange(setPH10);

    zbBinaryORP.addBinaryOutput();
    zbBinaryORP.setBinaryOutputApplication(BINARY_OUTPUT_APPLICATION_TYPE_HVAC_OTHER);
    zbBinaryORP.setBinaryOutputDescription("ORP256 Calibration");
    zbBinaryORP.onBinaryOutputChange(setORP);

    /* ── Valeurs des solutions (analog output) ← Nouveau ──────── */
    // pH 4 : slider 3.00 → 6.00, pas 0.01
    zbValPH4.addAnalogOutput();
    zbValPH4.setAnalogOutputDescription("Valeur solution pH4 (ex: 4.01)");
    zbValPH4.setAnalogOutputApplication(ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
    zbValPH4.setAnalogOutputResolution(0.01);
    zbValPH4.onAnalogOutputChange(onPH4ValChange);

    // pH 7 : slider 5.50 → 8.50
    zbValPH7.addAnalogOutput();
    zbValPH7.setAnalogOutputDescription("Valeur solution pH7 (ex: 7.00)");
    zbValPH7.setAnalogOutputApplication(ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
    zbValPH7.setAnalogOutputResolution(0.01);
    zbValPH7.onAnalogOutputChange(onPH7ValChange);

    // pH 10 : slider 8.50 → 11.50
    zbValPH10.addAnalogOutput();
    zbValPH10.setAnalogOutputDescription("Valeur solution pH10 (ex: 10.01)");
    zbValPH10.setAnalogOutputApplication(ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
    zbValPH10.setAnalogOutputResolution(0.01);
    zbValPH10.onAnalogOutputChange(onPH10ValChange);

    // ORP : slider 100 → 500 mV, pas 1
    zbValORP.addAnalogOutput();
    zbValORP.setAnalogOutputDescription("Valeur solution ORP (ex: 256 mV)");
    zbValORP.setAnalogOutputApplication(ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
    zbValORP.setAnalogOutputResolution(1.0);
    zbValORP.onAnalogOutputChange(onORPValChange);

    /* ── Enregistrement de tous les endpoints ─────────────────── */
    Zigbee.addEndpoint(&zbTempSensor);
    Zigbee.addEndpoint(&zbPressureSensor);
    Zigbee.addEndpoint(&zbAnalogPH);
    Zigbee.addEndpoint(&zbAnalogORP);
    Zigbee.addEndpoint(&zbBinaryPH4);
    Zigbee.addEndpoint(&zbBinaryPH7);
    Zigbee.addEndpoint(&zbBinaryPH10);
    Zigbee.addEndpoint(&zbBinaryORP);
    Zigbee.addEndpoint(&zbValPH4);
    Zigbee.addEndpoint(&zbValPH7);
    Zigbee.addEndpoint(&zbValPH10);
    Zigbee.addEndpoint(&zbValORP);

    // Antenne externe UFL
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);
    delay(100);
    pinMode(14, OUTPUT);
    digitalWrite(14, HIGH);

    Serial.println("Demarrage Zigbee...");
    if (!Zigbee.begin(ZIGBEE_ROUTER)) {
        Serial.println("Echec Zigbee ! Reboot...");
        ESP.restart();
    }
    Serial.print("Connexion au reseau ");
    while (!Zigbee.connected()) { Serial.print("."); delay(100); }
    Serial.println(" Connecte");

    zbTempSensor.requestOTAUpdate();

    /* ── Chargement NVS ───────────────────────────────────────── */
    calib.begin("calibration", false);

    // Tensions mesurées lors des calibrations
    ph401.mv   = calib.getFloat("v401",   -2857.3f);
    ph401.t    = calib.getFloat("t401",   20.0f);
    ph700.mv   = calib.getFloat("v700",    0.0f);
    ph700.t    = calib.getFloat("t700",   20.0f);
    ph1001.mv  = calib.getFloat("v1001",   2857.3f);
    ph1001.t   = calib.getFloat("t1001",  20.0f);
    orp256.mv  = calib.getFloat("orp256",  256.0f);
    orp256.t   = calib.getFloat("t256",   20.0f);

    // Valeurs des solutions (modifiables depuis Z2M)  ← Nouveau
    ph401.ph   = calib.getFloat("ph401",   4.01f);
    ph700.ph   = calib.getFloat("ph700",   7.00f);
    ph1001.ph  = calib.getFloat("ph1001", 10.01f);
    orp256.ph  = calib.getFloat("orp",    256.0f);

    pression_max = calib.getFloat("pression_max", 2068.0f);

    // Envoyer les valeurs actuelles vers Z2M au démarrage
    // (permet à Z2M/HA d'afficher la valeur stockée dès la connexion)
    zbValPH4.setAnalogOutput(ph401.ph);
    zbValPH7.setAnalogOutput(ph700.ph);
    zbValPH10.setAnalogOutput(ph1001.ph);
    zbValORP.setAnalogOutput(orp256.ph);

    // À partir d'ici les callbacks de valeur de solution sont opérationnels
    initialized = true;

    if (computePolyCoeffs()) {
        Serial.println("Equation polynomiale activee (3 points).");
    } else {
        Serial.println("Fallback lineaire (polynome invalide).");
    }

    xTaskCreate(sensors_values_update, "sensors_update", 8192, NULL, 2, NULL);

    zbTempSensor.setReporting(0, 60, 0.2);
    zbPressureSensor.setReporting(0, 60, 10);
    zbAnalogPH.setAnalogInputReporting(0, 60, 0.05);
    zbAnalogORP.setAnalogInputReporting(0, 60, 10);
}

/* ══════════════════════════════════════════════════════════════
 *  LOOP
 * ══════════════════════════════════════════════════════════════ */
void loop() {
    if (digitalRead(button) == LOW) {
        delay(100);
        int startTime = millis();
        while (digitalRead(button) == LOW) {
            delay(50);
            if ((millis() - startTime) > 3000) {
                Serial.println("Factory reset Zigbee et reboot dans 1s.");
                delay(1000);
                Zigbee.factoryReset();
            }
        }
    }
    delay(100);
}

/* ══════════════════════════════════════════════════════════════
 *  CALLBACKS — VALEURS DES SOLUTIONS (analog output)  ← Nouveau
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Appelé par Z2M/HA quand l'utilisateur modifie la valeur cible pH4.
 *        Mise à jour en RAM + NVS + recalcul du polynôme.
 */
void onPH4ValChange(float val) {
    if (!initialized) return;   // ignore appels spurieux au démarrage
    // Garde-fou plage physique cohérente
    if (val < 3.0f || val > 6.5f) {
        Serial.printf("WARN: Valeur pH4 hors plage [3.0-6.5] : %.2f ignoree\r\n", val);
        return;
    }
    ph401.ph = val;
    calib.putFloat("ph401", val);
    Serial.printf("Solution pH4 mise a jour : %.2f\r\n", val);
    computePolyCoeffs();
    kfPH.reset();
}

void onPH7ValChange(float val) {
    if (!initialized) return;
    if (val < 5.5f || val > 8.5f) {
        Serial.printf("WARN: Valeur pH7 hors plage [5.5-8.5] : %.2f ignoree\r\n", val);
        return;
    }
    ph700.ph = val;
    calib.putFloat("ph700", val);
    Serial.printf("Solution pH7 mise a jour : %.2f\r\n", val);
    computePolyCoeffs();
    kfPH.reset();
}

void onPH10ValChange(float val) {
    if (!initialized) return;
    if (val < 8.5f || val > 11.5f) {
        Serial.printf("WARN: Valeur pH10 hors plage [8.5-11.5] : %.2f ignoree\r\n", val);
        return;
    }
    ph1001.ph = val;
    calib.putFloat("ph1001", val);
    Serial.printf("Solution pH10 mise a jour : %.2f\r\n", val);
    computePolyCoeffs();
    kfPH.reset();
}

void onORPValChange(float val) {
    if (!initialized) return;
    if (val < 50.0f || val > 700.0f) {
        Serial.printf("WARN: Valeur ORP hors plage [50-700] mV : %.1f ignoree\r\n", val);
        return;
    }
    orp256.ph = val;
    calib.putFloat("orp", val);
    Serial.printf("Solution ORP mise a jour : %.1f mV\r\n", val);
    kfORP.reset();
}

/* ══════════════════════════════════════════════════════════════
 *  CALLBACKS — DÉCLENCHEURS DE CALIBRATION (binary output)
 * ══════════════════════════════════════════════════════════════ */
void setPH4(bool value)  { Serial.printf("Calib pH4(%d)  : %f\r\n", value, ph401.ph); ph401.cal  = value; }
void setPH7(bool value)  { Serial.printf("Calib pH7(%d)  : %f\r\n", value, ph700.ph); ph700.cal  = value; }
void setPH10(bool value) { Serial.printf("Calib pH10(%d) : %f\r\n", value, ph1001.ph); ph1001.cal = value; }
void setORP(bool value)  { Serial.printf("Calib ORP(%d)  : %f\r\n", value, orp256.ph); orp256.cal = value; }

/* ══════════════════════════════════════════════════════════════
 *  LECTURES CAPTEURS
 * ══════════════════════════════════════════════════════════════ */
float readTemp() {
    ds.request();
    delay(750);
    float t;
    ds.getTemp(addr[0], t);
    Serial.printf("Temp : %.2f deg C\r\n", t);
    return t;
}

uint16_t readPressure() {
    uint32_t mv  = analogReadMilliVolts(A0);
    uint16_t hPa = (uint16_t)((float)mv / 3300.0f * pression_max);
    Serial.printf("Pression : %d hPa\r\n", hPa);
    return hPa;
}

float readPHmV() {
    int16_t v = ADS.readADC_Differential_0_1();
    return ADS.toVoltage(v) * 1000.0f;
}

float readORPmV() {
    int16_t v = ADS.readADC_Differential_2_3();
    return ADS.toVoltage(v) * 1000.0f / 2.32f;
}

float readPH() {
    float mv      = readPHmV();
    float T       = readTemp();
    float S_T     = NERNST_K * (273.15f + T);
    float ph_raw  = mVtopH(mv, T);
    float ph_filt = kfPH.update(ph_raw);
    Serial.printf("pH  mv=%.2f  T=%.1f degC  S(T)=%.3f mV/pH  brut=%.3f  Kalman=%.3f\r\n",
                  mv, T, S_T, ph_raw, ph_filt);
    return ph_filt;
}

float readORP() {
    float mv_raw  = readORPmV();
    float mv256   = mv_raw + (orp256.ph - orp256.mv);
    float orp_filt = kfORP.update(mv256);
    Serial.printf("ORP brut=%.1f mV  Kalman=%.1f mV\r\n", mv256, orp_filt);
    return orp_filt;
}

/* ══════════════════════════════════════════════════════════════
 *  CALCUL DU POLYNÔME (3 points)
 * ══════════════════════════════════════════════════════════════ */
bool computePolyCoeffs() {
    float mv[3]  = { ph401.mv,  ph700.mv,  ph1001.mv  };
    float pH_[3] = { ph401.ph,  ph700.ph,  ph1001.ph  };

    float M[3][4];
    for (int i = 0; i < 3; i++) {
        M[i][0] = mv[i] * mv[i];
        M[i][1] = mv[i];
        M[i][2] = 1.0f;
        M[i][3] = pH_[i];
    }

    for (int col = 0; col < 3; col++) {
        int   maxRow = col;
        float maxVal = fabsf(M[col][col]);
        for (int row = col + 1; row < 3; row++) {
            if (fabsf(M[row][col]) > maxVal) { maxVal = fabsf(M[row][col]); maxRow = row; }
        }
        if (maxVal < 1e-8f) { poly_valid = false; return false; }
        if (maxRow != col) {
            for (int j = 0; j < 4; j++) { float t = M[col][j]; M[col][j] = M[maxRow][j]; M[maxRow][j] = t; }
        }
        float pivot = M[col][col];
        for (int j = col; j < 4; j++) M[col][j] /= pivot;
        for (int row = 0; row < 3; row++) {
            if (row == col) continue;
            float f = M[row][col];
            for (int j = col; j < 4; j++) M[row][j] -= f * M[col][j];
        }
    }

    poly_a = M[0][3]; poly_b = M[1][3]; poly_c = M[2][3];

    float e4  = fabsf(poly_a*ph401.mv *ph401.mv  + poly_b*ph401.mv  + poly_c - ph401.ph);
    float e7  = fabsf(poly_a*ph700.mv *ph700.mv  + poly_b*ph700.mv  + poly_c - ph700.ph);
    float e10 = fabsf(poly_a*ph1001.mv*ph1001.mv + poly_b*ph1001.mv + poly_c - ph1001.ph);
    poly_valid = (e4 < 0.01f) && (e7 < 0.01f) && (e10 < 0.01f);

    Serial.printf("Poly : a=%.8f b=%.6f c=%.4f [%s]\r\n",
                  poly_a, poly_b, poly_c, poly_valid ? "OK" : "INVALIDE");
    return poly_valid;
}

/* ══════════════════════════════════════════════════════════════
 *  CONVERSION mV → pH  (correction thermique par équation de Nernst)
 *
 *  Principe :
 *    1. Calculer la pente de Nernst à T mesurée   : S(T)   = NERNST_K*(273.15+T)
 *    2. Calculer la pente de Nernst à T calibration: S_cal  = NERNST_K*(273.15+T_cal)
 *    3. Ramener mv à la tension équivalente à T_cal via le point isopotentiel (pH 7) :
 *          mv_cal = mv_iso  +  (mv - mv_iso) * (S_cal / S_T)
 *    4. Appliquer le polynôme (ou droite) sur mv_cal
 *
 *  Le point isopotentiel (pH 7, mv_iso = ph700.mv) est le pH auquel la tension
 *  de l'électrode ne dépend pas de la température → pivot parfait pour Nernst.
 *
 *  Erreur résiduelle typique : < 0.01 pH sur 0–40°C
 * ══════════════════════════════════════════════════════════════ */
float mVtopH(float mv, float T = 20.0f) {
    // Pentes de Nernst [mV/pH]
    float S_T   = NERNST_K * (273.15f + T);        // à T courante
    float S_cal = NERNST_K * (273.15f + ph700.t);  // à T de calibration pH7

    // Normalisation Nernst : ramène mv à T_calibration
    float mv_cal = ph700.mv + (mv - ph700.mv) * (S_cal / S_T);

    if (poly_valid) {
        // Polynôme degré 2 sur mv normalisé → pH
        return poly_a * mv_cal * mv_cal + poly_b * mv_cal + poly_c;
    } else {
        // Fallback linéaire (2 points) sur mv normalisé
        float a_lin = (ph700.ph - ph401.ph) / (ph700.mv - ph401.mv);
        float b_lin =  ph700.ph - a_lin * ph700.mv;
        return a_lin * mv_cal + b_lin;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  CALIBRATION (acquisition des tensions de référence)
 * ══════════════════════════════════════════════════════════════ */
void calibration() {
    calibInProgress = true;
    if (ph401.cal) {
        for (int i = 0; i < 100; i++) { mVmoyen.addValue(readPHmV()); delay(100); }
        ph401.cal = false; ph401.mv = mVmoyen.getAverage(); ph401.t = readTemp(); mVmoyen.clear();
        calib.putFloat("v401", ph401.mv); calib.putFloat("t401", ph401.t);
        Serial.printf("-> pH %.2f calibre : %.2f mV  T=%.1f degC\r\n", ph401.ph, ph401.mv, ph401.t);
        zbBinaryPH4.setBinaryOutput(false); zbBinaryPH4.reportBinaryOutput();
        computePolyCoeffs(); kfPH.reset();
    }

    if (ph700.cal) {
        for (int i = 0; i < 100; i++) { mVmoyen.addValue(readPHmV()); delay(100); }
        ph700.cal = false; ph700.mv = mVmoyen.getAverage(); ph700.t = readTemp(); mVmoyen.clear();
        calib.putFloat("v700", ph700.mv); calib.putFloat("t700", ph700.t);
        Serial.printf("-> pH %.2f calibre : %.2f mV  T=%.1f degC\r\n", ph700.ph, ph700.mv, ph700.t);
        zbBinaryPH7.setBinaryOutput(false); zbBinaryPH7.reportBinaryOutput();
        computePolyCoeffs(); kfPH.reset();
    }

    if (ph1001.cal) {
        for (int i = 0; i < 100; i++) { mVmoyen.addValue(readPHmV()); delay(100); }
        ph1001.cal = false; ph1001.mv = mVmoyen.getAverage(); ph1001.t = readTemp(); mVmoyen.clear();
        calib.putFloat("v1001", ph1001.mv); calib.putFloat("t1001", ph1001.t);
        Serial.printf("-> pH %.2f calibre : %.2f mV  T=%.1f degC\r\n", ph1001.ph, ph1001.mv, ph1001.t);
        zbBinaryPH10.setBinaryOutput(false); zbBinaryPH10.reportBinaryOutput();
        computePolyCoeffs(); kfPH.reset();
    }

    if (orp256.cal) {
        for (int i = 0; i < 100; i++) { mVmoyen.addValue(readORPmV()); delay(100); }
        orp256.cal = false; orp256.mv = mVmoyen.getAverage(); orp256.t = readTemp(); mVmoyen.clear();
        calib.putFloat("orp256", orp256.mv); calib.putFloat("t256", orp256.t);
        Serial.printf("-> ORP %.0f mV calibre : %.2f mV\r\n", orp256.ph, orp256.mv);
        zbBinaryORP.setBinaryOutput(false); zbBinaryORP.reportBinaryOutput();
        kfORP.reset();
    }
    calibInProgress = false;
}

/* ══════════════════════════════════════════════════════════════
 *  TÂCHE CAPTEURS (FreeRTOS)
 * ══════════════════════════════════════════════════════════════ */
static void sensors_values_update(void *arg) {
    
    // Premier cycle : afficher le résumé de calibration.
    delay(500);
    printCalibSummary();

    for (;;) {
        if (otaInProgress) { delay(1000); continue; }
        if (calibInProgress) { delay(1000); continue; }

        float    temp     = readTemp();
        uint16_t pression = readPressure();
        float    ph       = readPH();
        float    orp      = readORP();

        zbTempSensor.setTemperature(temp);
        zbPressureSensor.setPressure(pression);
        zbTempSensor.reportTemperature();
        zbPressureSensor.report();
        zbAnalogPH.setAnalogInput(ph);
        zbAnalogPH.reportAnalogInput();
        zbAnalogORP.setAnalogInput(orp);
        zbAnalogORP.reportAnalogInput();

        calibration();
        delay(10000);
    }
}

/* ══════════════════════════════════════════════════════════════
 *  OTA
 * ══════════════════════════════════════════════════════════════ */
void otaStateChange(bool otaActive) {
    otaInProgress = otaActive;
    if (otaActive) {
        Serial.println("OTA en cours - suspension des mesures");
    } else {
        Serial.println("OTA terminee - redemarrage");
        delay(500);
        esp_restart();
    }
}
