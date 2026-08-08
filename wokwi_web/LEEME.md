# Variante para Wokwi web (navegador)

Los archivos de esta carpeta son **copias** de `src/`, reorganizadas en la
estructura `main/` que espera un proyecto ESP-IDF en wokwi.com.

---

## Cuándo conviene cada ruta

| | **Wokwi web** (navegador) | **VS Code + PlatformIO** |
|---|---|---|
| Preparación | ninguna | descarga del toolchain (varios cientos de MB) |
| Primer arranque | ~2 minutos | 15–40 min la primera vez |
| Simulación | ✅ | ✅ |
| **Grabar en la placa** | ❌ imposible | ✅ |
| Depuración con GDB | ❌ | ✅ |
| Control de versiones | manual | integrado |

### La recomendación práctica

**Usa las dos, en este orden.** Arranca la descarga del toolchain de
PlatformIO y, mientras baja, abre el navegador y verifica la lógica en Wokwi
web. Así no pierdes esos 20–40 minutos mirando una barra de progreso.

Pero el trabajo final tiene que pasar por PlatformIO de todas formas: la
evidencia en hardware real que exige la guía requiere grabar la placa, y eso
el navegador no puede hacerlo.

---

## Cómo abrirlo en el navegador

1. Entra a **https://wokwi.com/projects/new/esp32-idf**
   (crea un proyecto ESP-IDF vacío para ESP32).

2. Reemplaza el contenido de cada archivo:

   | Archivo en Wokwi | Contenido a pegar |
   |---|---|
   | `main/main.c` | `wokwi_web/main/main.c` |
   | `CMakeLists.txt` | `wokwi_web/CMakeLists.txt` |
   | `diagram.json` | `wokwi_web/diagram.json` |

3. Crea los archivos restantes con el botón **+** del panel de archivos:
   `main/energia.c`, `main/energia.h`, `main/indicador.c`, `main/indicador.h`,
   `main/config.h`

4. Reemplaza también `main/CMakeLists.txt` por `wokwi_web/main/CMakeLists.txt`
   — **este paso es el que más se olvida**. Sin él, Wokwi solo compila
   `main.c` y el enlazado falla con errores de símbolos indefinidos
   (`undefined reference to 'energia_light_sleep'`).

5. Presiona ▶ para compilar y simular.

---

## Por qué existe `config.h`

En PlatformIO los parámetros (pines, tiempos) llegan como `build_flags` desde
`platformio.ini`. **Wokwi web no lee ese archivo**, así que sin `config.h` el
proyecto no compilaría en el navegador.

`config.h` define cada macro solo si no llegó ya definida:

```c
#ifndef BOTON_WAKE_GPIO
#  define BOTON_WAKE_GPIO 33
#endif
```

Resultado: en PlatformIO mandan los `build_flags`; en el navegador mandan
estos valores por defecto. El mismo código fuente sirve para los dos entornos
sin ninguna modificación.

---

## Mantener las dos copias sincronizadas

Los archivos de `wokwi_web/main/` son copias. Si editas algo en `src/`,
vuelve a copiarlos:

**PowerShell (Windows):**
```powershell
Copy-Item src\*.c, src\*.h wokwi_web\main\ -Force
```

**Bash (Linux / macOS / Git Bash):**
```bash
cp src/*.c src/*.h wokwi_web/main/
```

> Si `src/` y `wokwi_web/main/` se desincronizan vas a depurar dos veces el
> mismo problema sin darte cuenta. Trata `src/` como la fuente de verdad y
> `wokwi_web/` como algo generado.

---

## Limitación importante de la simulación

Wokwi ejecuta correctamente la **lógica** de los modos de sueño: las
transiciones ocurren, el temporizador despierta el chip, el botón dispara
ext0 y la causa de despertar se reporta bien.

Lo que **no** hace es modelar corriente eléctrica. El ahorro energético —que
es el objetivo de esta tarea— solo se puede verificar con un multímetro sobre
hardware real. El procedimiento de medición está en la §7 del README
principal.
