# Library decision

## Current decision in v1.4.0

Preferred next backend path: **RadioLib**

Reason:
- passt gut zu Arduino + PlatformIO
- SX126x wird abgedeckt
- Projekt kann schrittweise weiterentwickelt werden, ohne sofort die komplette Struktur zu tauschen

Documented fallback:
- Semtech Basics Modem evaluieren, falls RadioLib auf dem XIAO/Wio-SX1262-Pfad unzureichend ist

## Important implementation constraint

Die Backend-Abstraktion `lorawan_backend.*` bleibt erhalten.
Die echte Library-Integration muss unterhalb dieser Abstraktion passieren, nicht quer durch `main.cpp`.
