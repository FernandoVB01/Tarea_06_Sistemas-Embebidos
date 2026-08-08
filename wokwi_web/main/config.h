/**
 * @file    config.h
 * @brief   Parametros de configuracion del sistema.
 *
 * Cada macro se define aqui SOLO si no llego ya definida desde fuera.
 * Esto permite dos entornos de compilacion sin tocar el codigo:
 *
 *   PlatformIO  -> los valores llegan por 'build_flags' en platformio.ini
 *                  y estos valores por defecto quedan ignorados.
 *
 *   Wokwi web   -> no existe platformio.ini, asi que se usan los valores
 *   / ESP-IDF      por defecto de este archivo.
 *
 * Para cambiar un parametro de forma permanente conviene editarlo aqui Y en
 * platformio.ini, o eliminar el flag correspondiente de platformio.ini para
 * que este archivo sea la unica fuente de verdad.
 */
#pragma once

/* ------------------------------------------------------------------ */
/*  LED RGB de estado (catodo comun)                                  */
/* ------------------------------------------------------------------ */
#ifndef LED_ROJO_GPIO
#  define LED_ROJO_GPIO   25   /* RTC_GPIO6  - indica DEEP SLEEP  */
#endif

#ifndef LED_VERDE_GPIO
#  define LED_VERDE_GPIO  26   /* RTC_GPIO7  - indica FASE ACTIVA */
#endif

#ifndef LED_AZUL_GPIO
#  define LED_AZUL_GPIO   27   /* RTC_GPIO17 - indica LIGHT SLEEP */
#endif

/* ------------------------------------------------------------------ */
/*  Boton de despertar externo (ext0)                                 */
/* ------------------------------------------------------------------ */
/*  DEBE ser un GPIO con funcion RTC: durante el deep sleep el dominio
 *  digital esta apagado y solo el subsistema RTC sigue vigilando.
 *  GPIO33 = RTC_GPIO8 segun la Tabla 2-1 del datasheet.              */
#ifndef BOTON_WAKE_GPIO
#  define BOTON_WAKE_GPIO 33
#endif

/* ------------------------------------------------------------------ */
/*  Tiempos del ciclo                                                 */
/* ------------------------------------------------------------------ */
#ifndef T_ACTIVO_LARGO_MS
#  define T_ACTIVO_LARGO_MS  8000       /* trabajo antes del light sleep */
#endif

#ifndef T_ACTIVO_CORTO_MS
#  define T_ACTIVO_CORTO_MS  4000       /* trabajo antes del deep sleep  */
#endif

#ifndef T_LIGHT_SLEEP_US
#  define T_LIGHT_SLEEP_US   5000000ULL /* 5 s dormido en light sleep    */
#endif

#ifndef T_DEEP_SLEEP_US
#  define T_DEEP_SLEEP_US    10000000ULL /* 10 s dormido en deep sleep   */
#endif

/* ------------------------------------------------------------------ */
/*  Ciclos completos antes de pasar a hibernacion                     */
/* ------------------------------------------------------------------ */
#ifndef CICLOS_ANTES_HIBERNAR
#  define CICLOS_ANTES_HIBERNAR 3
#endif
