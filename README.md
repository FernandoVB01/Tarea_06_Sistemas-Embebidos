# Tarea #6 — Gestión de Energía en el ESP32

**Sistemas Embebidos · FIEC · ESPOL**
Autor: **Fernando Vélez** · Framework **ESP-IDF** sobre **PlatformIO** (C, Visual Studio Code)

Firmware que alterna entre una fase de trabajo activo y una fase de reposo en
bajo consumo, con despertar por **temporizador RTC** y por **pulsador externo**,
e indicación de estado por **LED** y por puerto serial.

---

## 1. Descripción del ejercicio

| Fase | Duración | Comportamiento |
|---|---|---|
| **Proceso activo** | 10 s | CPU a plena velocidad, LED rojo parpadeando cada 500 ms |
| **Reposo** | 15 s o botón | `esp_light_sleep_start()` — la CPU se pausa |
| **Reactivación** | — | Se reporta el origen: `BOTON` o `TEMPORIZADOR AUTOMATICO` |

El ciclo se repite indefinidamente, con un contador que se incrementa en cada vuelta.

### Por qué light sleep y no un bucle de espera

Una espera implementada con un bucle de retardo produce **el mismo comportamiento
visible**: el LED se apaga y la consola anuncia el reposo. Pero el consumo no baja,
porque el núcleo sigue ejecutando instrucciones, y un multímetro no mide ninguna
diferencia.

La reducción real solo aparece al llamar a `esp_light_sleep_start()`, que pausa la
CPU de verdad:

| Estado | Consumo típico |
|---|---|
| Activo | ~40 mA |
| Light sleep | ~0.8 mA |

Cifras de la **Tabla 4-2 (Power Consumption by Power Modes)** del *ESP32 Series
Datasheet v5.2*.

---

## 2. Conexiones

| Señal | GPIO | Componente | Nota |
|---|---|---|---|
| LED `PROCESO` | **2** | LED rojo + 220 Ω | indica trabajo activo |
| LED `ESPERA` | **4** | LED amarillo + 220 Ω | marca la entrada en reposo |
| Botón `DESPERTAR` | **5** | Pulsador a GND | pull-up interno, activo en bajo |
| GND | GND | — | cátodos de los LEDs y pulsador |

El pulsador no lleva resistencia externa: se habilita el pull-up interno del ESP32,
de modo que el pin lee 1 en reposo y 0 al pulsarlo.

### Por qué el despertar es por GPIO y no por ext0

`esp_sleep_enable_ext0_wakeup()` **exige un pin con función RTC**, porque en deep
sleep el dominio digital está apagado y solo el subsistema RTC sigue vigilando.
**GPIO5 no tiene función RTC**, así que esa llamada devolvería `ESP_ERR_INVALID_ARG`.

En light sleep el dominio digital permanece alimentado, por lo que
`gpio_wakeup_enable()` sirve para cualquier pin. Migrar este firmware a deep sleep
obligaría a reubicar el pulsador en un GPIO con función RTC (por ejemplo GPIO33 =
`RTC_GPIO8`).

### El LED se apaga antes de dormir

Un LED encendido consume miliamperios; el light sleep está en el orden de 0.8 mA.
Dejarlo prendido durante el reposo anularía por completo el ahorro que se busca
demostrar, sin dejar ningún rastro en el código. Por eso `modoReposo()` apaga el LED
de espera justo antes de `esp_light_sleep_start()`.

---

## 3. Estructura del proyecto

```
Tarea6_Ahorro_Energia/
├── platformio.ini        Configuración; versión del framework FIJADA
├── CMakeLists.txt        Proyecto ESP-IDF
├── diagram.json          Circuito de Wokwi
├── wokwi.toml            Enlace al firmware compilado
├── merge_firmware.py     Fusiona bootloader+particiones+app para Wokwi
├── README.md             Este archivo
├── COMO_LEVANTAR.md      Guía de GitHub, VS Code y Wokwi
├── docs/
│   ├── informe.pdf       Informe entregable
│   └── capturas/         Imágenes del informe
└── src/
    ├── CMakeLists.txt
    └── main.c            Todo el firmware
```

---

## 4. Compilación y ejecución

### Compilar

```bash
pio run
```

> ⚠️ **La versión del framework está fijada a propósito** en `platformio.ini`
> (`platform = espressif32@5.4.0`, ESP-IDF 4.4.5). Sin fijarla, PlatformIO instala
> una versión más reciente cuyo binario **no arranca en el simulador de Wokwi**: la
> simulación corre, el cronómetro avanza, pero el chip nunca ejecuta nada y el monitor
> serial queda en blanco, ni siquiera con el volcado de la ROM. No quitar el `@5.4.0`
> sin volver a comprobar la simulación.

### Grabar en la placa

```bash
pio run --target upload
```

### Monitor serial

```bash
pio device monitor
```

### Simular en Wokwi

1. `pio run` — genera `firmware.elf` y `merged.bin`
2. `Ctrl+Shift+P` → **Wokwi: Start Simulator**
3. El pulsador del diagrama reactiva el sistema durante el reposo.

> **`wokwi.toml` apunta a `merged.bin`, no a `firmware.bin`.** Arrancar un ESP32
> requiere tres binarios: bootloader en `0x1000`, tabla de particiones en `0x8000` y
> aplicación en `0x10000`. `firmware.bin` es solo la aplicación; con ese archivo suelto
> el simulador arranca un flash sin bootloader y no sale nada por el serial.
> `merge_firmware.py` los fusiona tras cada `pio run`.

---

## 5. Evidencia en hardware real

**El consumo no se puede medir en simulación.** Wokwi ejecuta correctamente la lógica
de los modos de sueño, pero no modela corriente eléctrica.

Se mide con el multímetro en modo corriente, **en serie** con la alimentación:

```
  Fuente 5 V ──── [A] multímetro ──── VIN del ESP32
                                        │
                                       GND ──── GND fuente
```

| Fase | Lectura esperada en una DevKit |
|---|---|
| Activa (LED encendido) | 50 – 80 mA |
| Light sleep | 3 – 10 mA ⚠️ |

> ⚠️ Los 0.8 mA del datasheet corresponden al **chip desnudo**. Una DevKit consume
> bastante más por el regulador AMS1117 y el conversor USB-serial, que siguen
> alimentados. **No es un error del montaje**: es el hallazgo más interesante de la
> práctica y conviene documentarlo.

---

## 6. Conclusiones y recomendaciones

Desarrolladas en `docs/informe.pdf`. En resumen:

**Conclusiones**

1. El ahorro viene de pausar la CPU, no de apagar el indicador. El comportamiento
   observable y el comportamiento energético son cosas distintas.
2. La elección del pin condiciona qué modo de despertar existe: `ext0` exige función
   RTC, el despertar por GPIO no.
3. La placa de desarrollo impone un piso de consumo que el datasheet no muestra.

**Recomendaciones**

1. Apagar explícitamente todo lo que consuma antes de dormir, y comprobarlo midiendo.
2. Fijar la versión del toolchain en el control de versiones.

---

## 7. Entregables

- [x] Código fuente organizado
- [x] README explicativo
- [x] Informe PDF con análisis, conclusiones y recomendaciones
- [ ] Capturas de la simulación en Wokwi
- [ ] Fotos de la medición con multímetro
- [ ] Video de demostración en YouTube
