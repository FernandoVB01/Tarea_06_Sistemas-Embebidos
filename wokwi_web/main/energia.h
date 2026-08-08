/**
 * @file    energia.h
 * @brief   Gestion de los modos de bajo consumo del ESP32.
 *
 * Encapsula la configuracion explicita de cada modo de ahorro y de sus
 * fuentes de despertar, de modo que main.c exprese unicamente la logica
 * del ciclo y no los detalles del hardware.
 *
 * MODOS IMPLEMENTADOS
 * -------------------
 *   Light Sleep : la CPU se pausa pero NO se reinicia. Al despertar, la
 *                 ejecucion continua en la linea siguiente y toda la RAM
 *                 conserva su contenido. Consumo tipico: 0.8 mA.
 *
 *   Deep Sleep  : se apagan CPU y RAM principal. Al despertar el chip
 *                 REINICIA desde app_main(). Solo sobreviven las variables
 *                 marcadas con RTC_DATA_ATTR. Consumo tipico: 10 uA.
 *
 *   Hibernacion : deep sleep + apagado de la memoria RTC y del oscilador
 *                 de 8 MHz. Ni siquiera RTC_DATA_ATTR sobrevive. Solo
 *                 despierta por temporizador RTC o RTC GPIO. Consumo: 5 uA.
 *
 * (Cifras de consumo tomadas de la Tabla 4-2 del datasheet ESP32 v5.2.)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

/** Causa por la que el sistema arranco o desperto. */
typedef enum {
    DESPERTAR_ENCENDIDO = 0,  /* arranque en frio / reset            */
    DESPERTAR_TIMER,          /* vencio el temporizador RTC          */
    DESPERTAR_BOTON,          /* pin externo ext0                    */
    DESPERTAR_OTRO,
} causa_despertar_t;

/**
 * @brief Configura el GPIO del boton de despertar externo.
 *
 * Debe llamarse una vez al arrancar. El pin se configura con pull-up
 * interno, de modo que el boton solo tiene que cortocircuitarlo a GND.
 */
void energia_init_boton(void);

/** Determina por que se esta ejecutando app_main() en este momento. */
causa_despertar_t energia_causa_despertar(void);

/** Texto legible de la causa de despertar (para el log). */
const char *energia_causa_texto(causa_despertar_t causa);

/** true si el boton esta presionado en este instante. */
bool energia_boton_presionado(void);

/**
 * @brief Espera a que el boton se suelte.
 *
 * Evita que un boton mantenido presionado dispare inmediatamente el
 * siguiente despertar por ext0, lo que produciria un bucle de
 * dormir-despertar sin pausa.
 *
 * @param timeout_ms Tiempo maximo de espera.
 */
void energia_esperar_soltar_boton(int timeout_ms);

/**
 * @brief Entra en LIGHT SLEEP y RETORNA al despertar.
 * @param us_timer Microsegundos del temporizador de despertar.
 * @return Causa por la que se desperto.
 */
causa_despertar_t energia_light_sleep(uint64_t us_timer);

/**
 * @brief Entra en DEEP SLEEP. Esta funcion NO RETORNA NUNCA.
 *
 * El chip se reinicia al despertar y la ejecucion recomienza en app_main().
 *
 * @param us_timer Microsegundos del temporizador de despertar.
 */
void energia_deep_sleep(uint64_t us_timer);

/**
 * @brief Entra en HIBERNACION. Esta funcion NO RETORNA NUNCA.
 *
 * Apaga los dominios de memoria RTC, por lo que se pierde el contenido de
 * las variables RTC_DATA_ATTR. Solo se habilita el despertar por
 * temporizador: ext0 requiere el dominio RTC_PERIPH, que aqui se apaga.
 *
 * @param us_timer Microsegundos del temporizador de despertar.
 */
void energia_hibernar(uint64_t us_timer);
