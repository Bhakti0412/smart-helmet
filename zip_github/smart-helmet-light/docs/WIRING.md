# Wiring and pin connections

Pin numbers are for an **ESP32 DevKit V1** and are defined in `firmware/src/config.h` (change them there if you rewire).
**All grounds must be common.**

## Pin map

| ESP32 pin | Connects to | Notes |
|---|---|---|
| `3V3` | MPU6050 `VCC`, one leg of the LDR, (NEO-6M `VCC` if used) | 3.3 V logic everywhere except the LED strips |
| `GND` | MPU6050 `GND`, 10 k resistor, buzzer driver emitter, strips `GND`, TP4056/boost `GND` | common ground |
| `GPIO21` | MPU6050 `SDA` | I2C |
| `GPIO22` | MPU6050 `SCL` | I2C. MPU6050 `AD0` -> GND (address 0x68), `INT` unused |
| `GPIO34` | junction of LDR and 10 k resistor | input-only ADC1 pin |
| `GPIO18` | Front strip `DIN` via 330 ohm | warm white |
| `GPIO19` | Rear strip `DIN` via 330 ohm | red |
| `GPIO23` | Amber strip `DIN` via 330 ohm | amber indicators |
| `GPIO25` | Buzzer driver (see below) | active buzzer |
| `GPIO16` (RX2) | NEO-6M `TX` | optional GPS, set `ENABLE_GPS 1` |
| `GPIO17` (TX2) | NEO-6M `RX` | optional GPS |
| `VIN` (5 V) | 5 V rail from the boost converter | |

## Light sensor (LDR)

```
3V3 ---[ LDR ]---+---[ 10 k ]--- GND
                 |
              GPIO34
```
Brighter light -> lower LDR resistance -> **higher** reading. Dark -> low reading. Print the raw value (`ldr` in the BLE status
message) in daylight and at night and adjust `LDR_DARK_ON` / `LDR_BRIGHT_OFF` in `config.h`.

## Buzzer (active, 3-5 V)

```
5V ---- buzzer(+)
        buzzer(-) ---- collector  [2N2222 / BC547]
GPIO25 ---[ 1 k ]---- base
GND ------------------ emitter
```
A very small active buzzer can be driven straight from GPIO25 (<= ~10 mA), but a transistor is safer for the pin.

## LED strips (WS2812B)

```
5V rail ---+--- strip VCC (front, rear, amber)      1000 uF capacitor across 5V/GND near the strips
GND -------+--- strip GND
GPIOxx --[330 ohm]-- strip DIN
```
- Strips: **front 8 LEDs** warm white, **rear 8 LEDs** red, **amber 4 LEDs** (2 left, 2 right). Change `NUM_LEDS_*` for other lengths.
- Brightness is capped at 80/255 (`LED_BRIGHTNESS`) to keep the current draw within what one Li-ion cell and a small boost converter can supply.
- Power budget: a WS2812B draws up to 60 mA at full white, so 20 LEDs can reach 1.2 A. At the default brightness and with single-colour use the draw is a fraction of that, but size the boost converter for >= 1 A.
- 3.3 V data into a 5 V strip is slightly outside the WS2812B datasheet but usually works over short wires with the 330 ohm resistor. If the strips flicker, a 3.3 V -> 5 V level shifter is the standard fix (not part of the specified hardware list).

## Power and charging (TP4056 + Li-ion)

```
USB ---> TP4056 (IN+/IN-)
Li-ion cell + ---> TP4056 B+      cell - ---> TP4056 B-
TP4056 OUT+ ---> [ slide switch ] ---> 5 V boost converter ---> 5 V rail (ESP32 VIN + LED strips)
TP4056 OUT- ---> GND
```
- Use a **TP4056 board with the DW01 protection IC** (over-discharge / short-circuit protection) or a protected cell.
- The boost converter (3.7 V -> 5 V, >= 1 A) is needed because the ESP32 dev board and the strips want 5 V; it is a power-supply part of the TP4056/Li-ion subsystem.
- Do not connect the ESP32's USB cable (programming) and the battery 5 V rail to `VIN` at the same time without a diode: the two supplies would back-feed each other. Flash with the battery rail off.

## Mechanical notes

- **MPU6050 mounting:** fix it rigidly to the helmet shell, X axis pointing forward and Z axis pointing up. If it points backwards, set `BRAKE_AXIS_SIGN` to `-1.0f`.
- **LDR:** on the top/front of the shell, facing out, not covered by the front light.
- **Reflective tape/film:** stick on the rear and the sides of the shell (passive, no wiring).
- **Buzzer:** vent towards the rider's ear side so the countdown is audible under the helmet.
