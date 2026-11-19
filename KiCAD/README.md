# Terrarium controller – KiCad schematic

`Terrarium.kicad_sch` / `Terrarium.kicad_pro` (KiCad 10.0.8, A3, single sheet).

Validation: `kicad-cli sch erc --severity-all Terrarium.kicad_sch` → **0 errors, 0 warnings**.
Standard KiCad symbols are embedded in the sheet, so no extra symbol libraries are required to open it.

## Power supply (the missing "Spannungsversorgung" spec)

The parts list did not specify how the rails are generated. Implemented solution:

* `J1` VIN, 7–30 V DC (e.g. 12 V), reverse-polarity protection by Schottky `D1`, bulk cap `C1`.
* Two **MIC4684YM** buck converters, both fed directly from VIN:
  * `U1` → **+5 V / 2 A** (pump, heater, fan, moisture sensor, L293D VCC1/VCC2)
  * `U2` → **+3 V3 / 2 A** (STM32, BME680, OLED, ESP32 interface, I²C pull-ups)
* Feedback resistors: VFB = 1.235 V; 3.01 kΩ / 976 Ω → 5.04 V; 3.01 kΩ / 1.80 kΩ → 3.30 V.
* Both converters run from VIN rather than 5 V → 3 V3, because the MIC4684 needs roughly 3 V headroom at full load.

## Connectors (all off-board elements)

| Connector | Off-board device |
|---|---|
| J1 | VIN power input (7–30 V DC) |
| J2 | Pump, 5 V, driven by L293D half-bridge channel 1 |
| J3 | PTC heater, 5 V, low-side NMOS switch |
| J4 | Fan Noctua NF-A6x25 5 V PWM (4-pin: GND / +5 V / TACH / PWM) |
| J5 | Crowtail CT0007MS soil moisture sensor, 5 V, analog output |
| J6 | OLED GME12864-11 (SSD1306), I²C, 4-pin |
| J7 | ESP32 WLAN module, UART bridge |
| J8 | SWD debug header |

Wasserschlauch (water hose) and Gewächshaus (greenhouse) are mechanical parts, not electrical components, so they have no connector.

## On-board blocks

* `U3` STM32F103C8T6 (LQFP48), internal RTC on VBAT.
* `BT1` CR2032 holder with 100 nF buffer cap; `X1` 32.768 kHz LSE crystal.
* `U4` BME680 (temperature / humidity / pressure / gas) on I²C1.
* `U5` L293D H-bridge for the pump; `Q1` NMOS for the heater.
* Reset/BOOT0 resistors, I²C pull-ups, decoupling and SWD header included.

## Points to verify before manufacturing

* Existing firmware in `Software/main/main.c` targets an **ESP32** directly. The schematic uses the STM32 as the on-board MCU and exposes WLAN as a UART module on `J7`; firmware and hardware must be aligned.
* Check the delivered moisture-sensor cable colour order (VCC/GND/AO) and the OLED pin order against the actual modules.
* L293D EN/IN polarity and heater shutdown behaviour should be confirmed with the real pump and PTC heater.
* BME680 SDO is tied to GND through `R16` (0 Ω) for the default I²C address; CSB is tied to VDDIO as required for I²C mode.
