"""
Genera .pio/build/<env>/merged.bin despues de cada compilacion.

POR QUE HACE FALTA
------------------
Arrancar un ESP32 con ESP-IDF necesita TRES binarios en tres direcciones:

    0x1000   bootloader.bin
    0x8000   partitions.bin
    0x10000  firmware.bin   (la aplicacion)

`pio run --target upload` los graba los tres. Pero Wokwi solo acepta UN
archivo en `wokwi.toml`, y si se le pasa unicamente `firmware.bin` el
simulador arranca un flash sin bootloader: el chip nunca salta a
app_main(), asi que no hay salida por serial ni parpadeo de LEDs. El
cronometro de Wokwi avanza y parece que todo funciona, lo que hace el
sintoma bastante confuso.

Este script fusiona los tres en una sola imagen de flash que empieza en
0x0, que es lo que `wokwi.toml` apunta.
"""

import json
import os
import subprocess

Import("env")  # noqa: F821  (lo inyecta PlatformIO)


PARTES = [
    ("0x1000", "bootloader.bin"),
    ("0x8000", "partitions.bin"),
    ("0x10000", "firmware.bin"),
]

# Valores por defecto si flasher_args.json no estuviera disponible.
FLASH_POR_DEFECTO = {"flash_mode": "dio", "flash_size": "keep", "flash_freq": "40m"}

# Valores que acepta `esptool merge_bin`. ESP-IDF 4.4 escribe "detect" en
# flash_size, que solo tiene sentido con un chip conectado: al fusionar no
# hay chip que interrogar, asi que se traduce a "keep" (respeta lo que ya
# diga la cabecera del bootloader).
VALIDOS = {
    "flash_mode": {"keep", "qio", "qout", "dio", "dout"},
    "flash_size": {"keep", "256KB", "512KB", "1MB", "2MB", "2MB-c1", "4MB",
                   "4MB-c1", "8MB", "16MB", "32MB", "64MB", "128MB"},
    "flash_freq": {"keep", "80m", "40m", "26m", "20m"},
}


def _ajustes_flash(build_dir):
    """Lee modo/tamano/frecuencia de flash del json que genera ESP-IDF."""
    ruta = os.path.join(build_dir, "flasher_args.json")
    try:
        with open(ruta) as fh:
            ajustes = json.load(fh)["flash_settings"]
    except (OSError, ValueError, KeyError):
        return dict(FLASH_POR_DEFECTO)

    # Sustituye cualquier valor que merge_bin no entienda (p.ej. "detect").
    return {
        clave: (ajustes.get(clave) if ajustes.get(clave) in permitidos else "keep")
        for clave, permitidos in VALIDOS.items()
    }


def generar_merged(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    salida = os.path.join(build_dir, "merged.bin")

    rutas = [(off, os.path.join(build_dir, nom)) for off, nom in PARTES]
    faltantes = [os.path.basename(p) for _, p in rutas if not os.path.isfile(p)]
    if faltantes:
        print("merged.bin: omitido, faltan %s" % ", ".join(faltantes))
        return

    ajustes = _ajustes_flash(build_dir)
    esptool = os.path.join(
        env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py"
    )

    cmd = [
        env.subst("$PYTHONEXE"), esptool,
        "--chip", env.subst("$BOARD_MCU"),
        "merge_bin", "-o", salida,
        "--flash_mode", ajustes["flash_mode"],
        "--flash_size", ajustes["flash_size"],
        "--flash_freq", ajustes["flash_freq"],
    ]
    for offset, ruta in rutas:
        cmd += [offset, ruta]

    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as err:
        # No abortamos la compilacion: el binario para la placa ya es valido,
        # lo unico que queda sin actualizar es la imagen para el simulador.
        print("merged.bin: fallo esptool (Wokwi usara la imagen anterior)")
        print(err.stderr.strip())
        return

    print("merged.bin: %d bytes -> %s" % (os.path.getsize(salida), salida))


# Se engancha al .bin de la aplicacion, no al alias "buildprog": ese alias no
# se dispara en todas las versiones de la plataforma espressif32 (con 5.4.0 no
# lo hace) y merged.bin quedaria sin regenerar en silencio.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", generar_merged)  # noqa: F821
