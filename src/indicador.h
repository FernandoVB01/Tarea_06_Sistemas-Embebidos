/**
 * @file    indicador.h
 * @brief   Indicacion visual del estado del sistema mediante LED RGB.
 *
 * Cada estado del sistema tiene un color asociado, de modo que el
 * comportamiento energetico se puede seguir a simple vista sin necesidad
 * del monitor serial:
 *
 *      VERDE      -> fase activa (consumo maximo)
 *      AZUL       -> a punto de entrar en light sleep
 *      ROJO       -> a punto de entrar en deep sleep
 *      MAGENTA    -> a punto de entrar en hibernacion
 *      APAGADO    -> el sistema esta durmiendo
 *
 * NOTA IMPORTANTE sobre el consumo:
 * Los LEDs se apagan SIEMPRE antes de dormir. Un LED encendido consume
 * del orden de miliamperios, mientras que el deep sleep del ESP32 esta
 * en el orden de microamperios (10 uA segun el datasheet). Dejar un LED
 * prendido anularia por completo el ahorro que se busca demostrar.
 */
#pragma once

#include <stdbool.h>

/** Estados visuales del sistema. */
typedef enum {
    IND_APAGADO = 0,
    IND_ACTIVO,        /* verde   */
    IND_LIGHT_SLEEP,   /* azul    */
    IND_DEEP_SLEEP,    /* rojo    */
    IND_HIBERNACION,   /* magenta */
} indicador_estado_t;

/** Configura los tres GPIOs del LED RGB como salidas. */
void indicador_init(void);

/** Fija el color correspondiente al estado indicado. */
void indicador_set(indicador_estado_t estado);

/** Apaga los tres canales. Se llama siempre antes de dormir. */
void indicador_apagar(void);

/**
 * @brief Parpadeo de cortesia para marcar una transicion.
 * @param estado  Color a parpadear.
 * @param veces   Numero de destellos.
 * @param ms      Duracion de cada semiciclo.
 */
void indicador_parpadeo(indicador_estado_t estado, int veces, int ms);

/**
 * @brief Libera el "hold" de los pines RTC tras un deep sleep.
 *
 * Si antes de dormir se congelo el estado de un pin con rtc_gpio_hold_en(),
 * al despertar queda bloqueado hasta que se libera explicitamente. Se llama
 * al arrancar para garantizar que los LEDs vuelvan a ser controlables.
 */
void indicador_liberar_hold(void);
