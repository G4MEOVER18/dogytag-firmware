# Hardware

## MCU
Seeed Studio XIAO ESP32S3 without Sense board.

## Fixed wiring
### Wio-SX1262
- DIO1 = D1 / GPIO1
- RST = D2 / GPIO2
- BUSY = D3 / GPIO3
- NSS = D4 / GPIO4
- RF_SW = D5 / GPIO5
- SCK = D8 / GPIO8
- MISO = D9 / GPIO9
- MOSI = D10 / GPIO10

### RA-01 / SX1278
- NSS = D0 / GPIO0
- RST = MTCK / GPIO39
- DIO0 = MTDO / GPIO40
- shared SPI on D8/D9/D10

### GPS
- TX -> D7 / GPIO7
- RX -> D6 / GPIO6
- PPS -> MTDI / GPIO41
