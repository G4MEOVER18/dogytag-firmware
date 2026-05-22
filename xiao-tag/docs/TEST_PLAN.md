# Test plan

## Stage 1 - bring-up
- boot log visible
- pin summary visible
- GPS UART bytes visible
- PPS interrupt visible

## Stage 2 - SX1278
- version register = 0x12
- mode test writes sleep/standby

## Stage 3 - SX1262
- status byte readable
- command path usable
- error read path usable

## Stage 4 - LoRaWAN
- local secrets set
- radio looks alive
- adapter enters join-pending
