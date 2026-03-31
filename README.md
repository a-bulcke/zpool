# zpool V1.0
Carte Zigbbe de gestion de piscine avec mesure pH, Orp, température et pression

**Sources :** 

https://github.com/Loic74650/pH_Orp_Board

https://www.360customs.de/fr/2019/12/orp-redox-ph-elektrometer-shield-fuer-wemos-d1-mini-mit-lmp7721-ads1115-adm3260/

https://www.ti.com/lit/ds/symlink/lmp7721.pdf?ts=1769807145680&ref_url=https%253A%252F%252Fwww.ti.com%252Fproduct%252FLMP7721

## Xiao ESP32C6
Utilisation d'un ESP32C6 pour en faire un routeur Zigbee.

Fonctionne avec Zigbee2Mqtt sous Home Assistant.

La carte mesure le pH, l’ Orp, la température et la pression.

La gestion des pompes est assurée par des prises Zigbee c’est plus simple que d’ajouter des relais à la carte.

<img src="/img/pcb_front.png" width="300"><img src="img/Zpool.png" width="300">
<img src="/img/zpool_schema.png" width="300"><img src="/img/boitier.png" width="300">
<img src="/img/carte_xiao.jpg" width="300"><img src="/img/zigbee2mqtt.png" width="150">
<img src="/img/zpoolv1.jpg" width="300">


![Capteur de température DS18B20](https://fr.aliexpress.com/item/32811388557.html?)
<img width="1264" height="490" alt="image" src="https://github.com/user-attachments/assets/7ca6f61c-7ac1-46bc-b690-50194a19e913" />


![Capteur de pression 30 Psi](https://fr.aliexpress.com/item/4001179110023.html?)
<img width="1232" height="472" alt="image" src="https://github.com/user-attachments/assets/8662b836-ded8-4d65-a672-80ff956d5b51" />


# Programmation #
## Arduino IDE ##
Dans File - Preferences... ajouter :

```https://espressif.github.io/arduino-esp32/package_esp32_index.json```

Choisir une carte `XIAO_ESP32C6`

Dans Tools choisir `Partition scheme: ```"Zigbee ZCZR 4MB with spiffs"``` et ```"Zigbee Mode: "ZCZR" (coordinator/router)```
<img width="718" height="263" alt="image" src="https://github.com/user-attachments/assets/00f9ca7a-6e85-4ec6-9284-5f51421cac6b" />

# Branchements #
## température et pression ##

<img width="442" height="219" alt="image" src="https://github.com/user-attachments/assets/0b99f36f-bbe0-44be-a799-b48a53e65cc8" />

## pH et Orp ##

<img width="682" height="788" alt="image" src="https://github.com/user-attachments/assets/ddd50721-c92d-4cd0-a86b-9bedb6fa5c35" />

# Zigbee2Mqtt #
Dans Zigbee2mqtt sous Home Assistant, laller dans le menu `Paramètres` puis `console de developpement - convertisseurs externes` :

<img width="932" height="485" alt="image" src="https://github.com/user-attachments/assets/3587991a-1328-4ba5-8004-b5564d57b140" />

Créer un nouvea convertisseur et coller je contenu de ![ZigbeePoolSensorsCalib.js](https://github.com/a-bulcke/zpool/blob/main/zigbee2mqtt/ZigbeePoolSensorsCalib.js)

<img width="445" height="243" alt="image" src="https://github.com/user-attachments/assets/ba7376f6-c386-4b8f-a940-1161fb9722e9" />

Redémarrer Zigbee2mqtt puis autoriser l'association.

# Mise à jour #

## Programmation ##

Arduino IDE

Dans File - Preferences... ajouter :

```https://espressif.github.io/arduino-esp32/package_esp32_index.json```

Choisir une carte XIAO_ESP32C6 :


<img width="678" height="305" alt="image" src="https://github.com/user-attachments/assets/d0d6836c-16a6-46c1-87e2-4705d107b142" />


<img width="360" height="305" alt="image" src="https://github.com/user-attachments/assets/5ee56a3f-e4a6-4aa4-a9af-70fcf8b1b8bd" />


Dans Tools choisir Partition scheme: "Zigbee ZCZR 4MB with spiffs" et Zigbee Mode: "ZCZR (coordinator/router)"

<img width="718" height="263" alt="image" src="https://github.com/user-attachments/assets/d69159c5-b0cd-445a-aaad-a5ce7c5304e6" />


Et copier coller le code dans Arduino IDE :

Mettre ```Erase All Flash before sketch Upload : Disabled``` pour conserver l'appairage zigbee

<img width="504" height="548" alt="image" src="https://github.com/user-attachments/assets/88b685ce-3d30-4234-acdd-36c6a44c3e30" />


Puis Upload :

<img width="432" height="128" alt="image" src="https://github.com/user-attachments/assets/c4538ff6-32a3-41d7-afa3-70f6845f523d" />

