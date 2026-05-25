## v2.6.1
- kleiner Konsolenschritt: neue Befehle `version`, `uptime`, `console`
- neue Schnellstart-Doku für serielle Befehle
- keine Architekturveraenderung

# Changelog

## v2.6.0
- added dedicated LoRaWAN preflight module for reusable readiness reporting
- added backendjson console command and richer backend code/error diagnostics
- exposed backend readiness and error fields in telemetry and status logging

## v2.5.0
- fixed broken escaped string literals in lorawan manager, runtime config and command console
- added LoRaWAN backend readiness and error codes to runtime info
- added backend console command and richer preflight diagnostics
- improved local checks to catch probable unescaped string literals

## v2.4.0
- continued integrated bring-up and developer workflow improvements
