import * as m from "zigbee-herdsman-converters/lib/modernExtend";

export default {
    zigbeeModel: ["ZPool"],
    model: "ZPool",
    vendor: "Bulcke",
    description: "ZPool v1.3.3 - pH/ORP/Temp/Pression, calibration 3 points ajustable",
    extend: [
        m.deviceEndpoints({
            endpoints: {
                // Mesures
                9: 9, 10: 10, 11: 11, 12: 12,
                // Déclencheurs calibration (binary output)
                4: 4, 5: 5, 6: 6, 7: 7,
                // Valeurs des solutions (analog output — slider)
                13: 13, 14: 14, 15: 15, 16: 16,
            }
        }),

        /* ════════════════════════════════════════════════════════
         *  MESURES
         * ════════════════════════════════════════════════════════ */
        m.numeric({
            name: "pH",
            unit: "pH",
            cluster: "genAnalogInput",
            attribute: "presentValue",
            reporting: {min: "MIN", max: "MAX", change: 1},
            description: "Mesure pH (Kalman + polynôme degré 2)",
            access: "STATE_GET",
            endpointNames: ["9"],
            precision: 2,
        }),
        m.numeric({
            name: "ORP",
            unit: "mV",
            cluster: "genAnalogInput",
            attribute: "presentValue",
            reporting: {min: "MIN", max: "MAX", change: 1},
            description: "Mesure ORP (filtre Kalman)",
            access: "STATE_GET",
            endpointNames: ["12"],
            precision: 2,
        }),
        m.temperature({endpointNames: ["10"]}),
        m.pressure({endpointNames: ["11"]}),
        m.numeric({
            name: "bar",
            unit: "bar",
            cluster: "msPressureMeasurement",
            attribute: "measuredValue",
            description: "Pression en bar",
            scale: 1000,
            precision: 2,
            access: "STATE",
        }),

        /* ════════════════════════════════════════════════════════
         *  DÉCLENCHEURS DE CALIBRATION (binary output)
         * ════════════════════════════════════════════════════════ */
        m.binary({
            name: "PH401 Calibration",
            cluster: "genBinaryOutput",
            attribute: "presentValue",
            reporting: {attribute: "presentValue", min: "MIN", max: "MAX", change: 1},
            valueOn: ["ON", 1],
            valueOff: ["OFF", 0],
            description: "Lancer la calibration pH point 1",
            access: "ALL",
            endpointName: "4",
        }),
        m.binary({
            name: "PH700 Calibration",
            cluster: "genBinaryOutput",
            attribute: "presentValue",
            reporting: {attribute: "presentValue", min: "MIN", max: "MAX", change: 1},
            valueOn: ["ON", 1],
            valueOff: ["OFF", 0],
            description: "Lancer la calibration pH point 2",
            access: "ALL",
            endpointName: "5",
        }),
        m.binary({
            name: "PH1001 Calibration",
            cluster: "genBinaryOutput",
            attribute: "presentValue",
            reporting: {attribute: "presentValue", min: "MIN", max: "MAX", change: 1},
            valueOn: ["ON", 1],
            valueOff: ["OFF", 0],
            description: "Lancer la calibration pH point 3",
            access: "ALL",
            endpointName: "7",
        }),
        m.binary({
            name: "ORP256 Calibration",
            cluster: "genBinaryOutput",
            attribute: "presentValue",
            reporting: {attribute: "presentValue", min: "MIN", max: "MAX", change: 1},
            valueOn: ["ON", 1],
            valueOff: ["OFF", 0],
            description: "Lancer la calibration ORP",
            access: "ALL",
            endpointName: "6",
        }),

        /* ════════════════════════════════════════════════════════
         *  VALEURS DES SOLUTIONS — SLIDERS (analog output)
         *
         *  Ces valeurs sont lues par l'ESP32 et stockées en NVS.
         *  Les modifier met à jour le polynôme sans re-flasher.
         * ════════════════════════════════════════════════════════ */
        m.numeric({
            name: "solution_ph4",
            label: "Valeur solution pH4",
            unit: "pH",
            cluster: "genAnalogOutput",
            attribute: "presentValue",
            reporting: {attribute: "presentValue", min: "MIN", max: "MAX", change: 0},
            description: "pH exact de la solution tampon point 1 (ex : 4.01 ou 6.86)",
            access: "ALL",
            endpointNames: ["13"],
            precision: 2,
            valueMin: 3.0,
            valueMax: 6.5,
            valueStep: 0.01,
        }),
        m.numeric({
            name: "solution_ph7",
            label: "Valeur solution pH7",
            unit: "pH",
            cluster: "genAnalogOutput",
            attribute: "presentValue",
            reporting: {attribute: "presentValue", min: "MIN", max: "MAX", change: 0},
            description: "pH exact de la solution tampon point 2 (ex : 7.00 ou 6.86)",
            access: "ALL",
            endpointNames: ["14"],
            precision: 2,
            valueMin: 5.5,
            valueMax: 8.5,
            valueStep: 0.01,
        }),
        m.numeric({
            name: "solution_ph10",
            label: "Valeur solution pH10",
            unit: "pH",
            cluster: "genAnalogOutput",
            attribute: "presentValue",
            reporting: {attribute: "presentValue", min: "MIN", max: "MAX", change: 0},
            description: "pH exact de la solution tampon point 3 (ex : 10.01 ou 9.18)",
            access: "ALL",
            endpointNames: ["15"],
            precision: 2,
            valueMin: 8.5,
            valueMax: 11.5,
            valueStep: 0.01,
        }),
        m.numeric({
            name: "solution_orp",
            label: "Valeur solution ORP",
            unit: "mV",
            cluster: "genAnalogOutput",
            attribute: "presentValue",
            reporting: {attribute: "presentValue", min: "MIN", max: "MAX", change: 0},
            description: "mV exact de la solution ORP (ex : 256 ou 240)",
            access: "ALL",
            endpointNames: ["16"],
            precision: 0,
            valueMin: 50,
            valueMax: 700,
            valueStep: 1,
        }),
    ],
    meta: {multiEndpoint: true},
};
