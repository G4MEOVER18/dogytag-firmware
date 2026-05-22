# Radio tests

## SX1278 test sequence in v1.7.0
1. Read RegVersion
2. Verify sleep and standby opmode writes
3. Prepare TX scaffold and payload
4. Enter TX mode and watch IRQ / DIO0 for TxDone
5. Enter RX continuous mode and watch IRQ / DIO0 for RxDone

## Expected healthy indicators
- version register = 0x12
- sleep opmode ~= 0x80
- standby opmode ~= 0x81
- tx mode opmode ~= 0x83
- rx mode opmode ~= 0x85

## Expected serial lines
- `[sx1278] smoke ... state=smoke-ok`
- `[sx1278] tx mode request ... state=tx-test-running`
- `[sx1278] tx done ...` or `[sx1278] tx test timeout ...`
- `[sx1278] rx mode request ... state=rx-test-running`
- `[sx1278] rx done ...` or `[sx1278] rx test timeout ...`
