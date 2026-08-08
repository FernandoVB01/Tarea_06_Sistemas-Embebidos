/**
 * =====================================================================
 *  TAREA #6 - SISTEMAS EMBEBIDOS (FIEC - ESPOL)
 *  Gestion de Energia en el ESP32
 *
 *  Autor: Fernando Velez
 * =====================================================================
 *
 *  CICLO DE OPERACION
 *  ------------------
 *
 *      [PROCESO ACTIVO]  10 s - LED ROJO parpadeando
 *           |            la CPU trabaja a plena velocidad
 *           v
 *      [REPOSO]  15 s o boton - LED AMARILLO marca la entrada
 *           |    el chip entra en LIGHT SLEEP: la CPU se pausa
 *           v
 *      [CAUSA DE REACTIVACION]  BOTON o TEMPORIZADOR
 *           |
 *           v
 *      (vuelve al PROCESO ACTIVO, con el contador incrementado)
 *
 *
 *  POR QUE LIGHT SLEEP Y NO UN BUCLE DE ESPERA
 *  -------------------------------------------
 *  Una espera hecha con vTaskDelay() mantiene la CPU encendida: el LED
 *  se apaga y el mensaje dice "reposo", pero el consumo NO baja y un
 *  multimetro no mide ninguna diferencia. Como el objetivo de la tarea
 *  es demostrar ahorro de energia medible, la fase de reposo usa
 *  esp_light_sleep_start(), que pausa la CPU de verdad (~0.8 mA frente
 *  a ~40 mA en activo, Tabla 4-2 del datasheet ESP32 v5.2).
 *
 *  Al despertar de light sleep la ejecucion CONTINUA en la linea
 *  siguiente y toda la RAM conserva su contenido, por eso el contador
 *  de ciclos es una variable normal y no necesita RTC_DATA_ATTR.
 * =====================================================================
 */
#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_sleep.h"

/* ===== DEFINICION DE PINES ===== */
#define LED_PROCESO     2   /* LED ROJO     - indica proceso activo */
#define LED_ESPERA      4   /* LED AMARILLO - indica modo reposo    */
#define BOTON_DESPERTAR 5   /* Activo en LOW (pull-up interno)      */

/* ===== CONFIGURACION DE TIEMPOS ===== */
#define DURACION_PROCESO_MS  10000   /* 10 segundos de trabajo          */
#define DURACION_REPOSO_SEG  15      /* 15 segundos en reposo (maximo)  */

/* ===== VARIABLES GLOBALES ===== */
static int contadorCiclos = 0;

/* ------------------------------------------------------------------ */
/*  Inicializacion                                                    */
/* ------------------------------------------------------------------ */

static void inicializarPines(void)
{
    const gpio_config_t leds = {
        .pin_bit_mask = (1ULL << LED_PROCESO) | (1ULL << LED_ESPERA),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&leds));

    /* Fuerza de salida al minimo (~5 mA en vez de los ~20 mA por defecto).
     *
     * Es una RED DE SEGURIDAD, no un sustituto de la resistencia: si un LED
     * se conecta sin sus 220 ohm en serie, a fuerza normal el pin intentaria
     * entregar 50-100 mA, muy por encima de los 12 mA recomendados por pin,
     * y acabaria degradando el driver. Con CAP_0 la corriente se queda en un
     * rango que el pin aguanta.
     *
     * Con resistencia puesta esto solo hace el LED ligeramente menos brillante.
     * La resistencia sigue siendo lo correcto.                              */
    ESP_ERROR_CHECK(gpio_set_drive_capability(LED_PROCESO, GPIO_DRIVE_CAP_0));
    ESP_ERROR_CHECK(gpio_set_drive_capability(LED_ESPERA,  GPIO_DRIVE_CAP_0));

    /* El boton cierra contra GND, por eso el pull-up interno: en reposo
     * el pin lee 1, y al pulsarlo lee 0.                              */
    const gpio_config_t boton = {
        .pin_bit_mask = (1ULL << BOTON_DESPERTAR),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boton));

    gpio_set_level(LED_PROCESO, 0);
    gpio_set_level(LED_ESPERA, 0);
}

static bool botonPresionado(void)
{
    return gpio_get_level(BOTON_DESPERTAR) == 0;   /* activo en bajo */
}

/* ------------------------------------------------------------------ */
/*  Fase 1: proceso activo                                            */
/* ------------------------------------------------------------------ */

/**
 * Simula la carga util del sistema durante DURACION_PROCESO_MS.
 * Es el estado de mayor consumo: CPU a plena velocidad y LED encendido.
 *
 * @return true si el boton se pulso en algun momento de la fase.
 */
static bool ejecutarProceso(void)
{
    printf("Sistema trabajando - realizando operaciones...\n");

    bool estadoLED       = false;
    bool botonDetectado  = false;
    int  transcurrido    = 0;

    while (transcurrido < DURACION_PROCESO_MS) {
        estadoLED = !estadoLED;
        gpio_set_level(LED_PROCESO, estadoLED ? 1 : 0);

        if (botonPresionado()) {
            botonDetectado = true;
            printf("  >>> BOTON detectado durante el proceso activo\n");
        }

        printf(" Realizando actividad\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        transcurrido += 500;
    }

    gpio_set_level(LED_PROCESO, 0);
    return botonDetectado;
}

/* ------------------------------------------------------------------ */
/*  Fase 2: reposo (light sleep)                                      */
/* ------------------------------------------------------------------ */

/**
 * Entra en LIGHT SLEEP hasta que venza el temporizador o se pulse el
 * boton, lo que ocurra primero.
 *
 * Dos fuentes de despertar:
 *   - Temporizador RTC : despertar periodico y predecible.
 *   - GPIO             : despertar por evento asincrono, sin gastar
 *                        energia sondeando el pin.
 *
 * Se usa despertar por GPIO y no ext0 porque ext0 exige un pin con
 * funcion RTC, y GPIO5 no la tiene. En light sleep el dominio digital
 * sigue alimentado, asi que gpio_wakeup_enable() vale para cualquier pin.
 *
 * @return true si desperto por el boton, false si vencio el temporizador.
 */
static bool modoReposo(void)
{
    printf(" Cambiando a REPOSO ...\n");

    /* Destello de aviso: se ve la transicion antes de dormir. */
    gpio_set_level(LED_ESPERA, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* El LED se apaga ANTES de dormir. Un LED encendido consume
     * miliamperios y anularia por completo el ahorro que se busca
     * demostrar: el light sleep esta en el orden de 0.8 mA.        */
    gpio_set_level(LED_ESPERA, 0);

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(
        (uint64_t)DURACION_REPOSO_SEG * 1000000ULL));

    ESP_ERROR_CHECK(gpio_wakeup_enable(BOTON_DESPERTAR, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    /* Vaciar la FIFO del UART antes de dormir. Si el chip se duerme con
     * bytes pendientes, salen recortados y el monitor muestra basura. */
    uart_wait_tx_idle_polling(UART_NUM_0);

    /* La CPU se pausa aqui. Al despertar continua en la linea siguiente
     * y la RAM esta intacta.                                          */
    esp_light_sleep_start();

    bool porBoton = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO);

    /* Si el usuario mantiene el boton pulsado, el siguiente sueno se
     * interrumpiria de inmediato. Se espera a que lo suelte.          */
    if (porBoton) {
        int espera = 0;
        while (botonPresionado() && espera < 3000) {
            vTaskDelay(pdMS_TO_TICKS(20));
            espera += 20;
        }
        vTaskDelay(pdMS_TO_TICKS(50));   /* antirrebote */
    }

    printf(" Regresando del reposo\n");
    return porBoton;
}

/* ------------------------------------------------------------------ */
/*  Fase 3: reporte de la causa                                       */
/* ------------------------------------------------------------------ */

static void mostrarCausaDespertar(bool botonEnProceso, bool botonEnReposo)
{
    if (botonEnProceso || botonEnReposo) {
        printf("[REACTIVACION] Origen: BOTON\n");
        for (int i = 0; i < 3; i++) {
            gpio_set_level(LED_ESPERA, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(LED_ESPERA, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    } else {
        printf("[REACTIVACION] Origen: TEMPORIZADOR AUTOMATICO\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Programa principal                                                */
/* ------------------------------------------------------------------ */

void app_main(void)
{
    inicializarPines();

    printf("\n=== Sistema operativo iniciado ===\n");
    printf("LED PROCESO = Trabajando\n");
    printf("LED ESPERA = En reposo\n");
    printf("Boton = Reactivar\n");
    printf("==========================================\n\n");

    while (1) {
        contadorCiclos++;
        printf("\n===========================================\n");
        printf("Ciclo de trabajo numero: %d\n", contadorCiclos);
        printf("===========================================\n");

        bool botonEnProceso = ejecutarProceso();
        bool botonEnReposo  = modoReposo();
        mostrarCausaDespertar(botonEnProceso, botonEnReposo);
    }
}
