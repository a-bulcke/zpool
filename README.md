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
<img src="/img/carte_xiao.jpg" width="300"><img src="/img/zigbee2mqtt.png" width="300">
<img src="/img/zpoolv1.jpg" width="300">

![Capteur de température DS18B20](https://fr.aliexpress.com/item/4001086169171.html)

![Capteur de pression 30 Psi](https://fr.aliexpress.com/item/4001179110023.html)

# Programmation #
## Arduino IDE ##
Dans File - Preferences... ajouter :

`https://espressif.github.io/arduino-esp32/package_esp32_index.json`

Choisir une carte `XIAO_ESP32C6`

Dans Tools choisir `Partition scheme: "Zigbee ZCZR 4MB with spiffs"` et `Zigbee Mode: "ZCZR (coordinator/router)"`
<img width="718" height="263" alt="image" src="https://github.com/user-attachments/assets/00f9ca7a-6e85-4ec6-9284-5f51421cac6b" />

