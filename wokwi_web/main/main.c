/**
 * =====================================================================
 *  TAREA #6 - SISTEMAS EMBEBIDOS (FIEC - ESPOL)
 *  Ejercicio 1: Gestion de Energia en el ESP32
 * =====================================================================
 *
 *  CICLO DE OPERACION
 *  ------------------
 *
 *      [ARRANQUE]  se reporta la causa del despertar
 *           |
 *           v
 *      [FASE ACTIVA LARGA]  8 s - LED VERDE
 *           |               trabajo simulado + telemetria por serial
 *           v
 *      [LIGHT SLEEP]  5 s o boton - LED APAGADO
 *           |         la CPU se pausa; al volver, la RAM esta intacta
 *           v
 *      [FASE ACTIVA CORTA]  4 s - LED VERDE
 *           |               se demuestra que la RAM sobrevivio
 *           v
 *      [DEEP SLEEP]  10 s o boton - LED APAGADO
 *           |        el chip REINICIA; solo sobrevive RTC_DATA_ATTR
 *           v
 *      (vuelve al ARRANQUE, con el contador de ciclos incrementado)
 *
 *      Tras CICLOS_ANTES_HIBERNAR ciclos completos, el sistema entra en
 *      HIBERNACION para demostrar que ni la memoria RTC sobrevive.
 *
 *  QUE DEMUESTRA CADA MODO
 *  -----------------------
 *    Light sleep : la variable de RAM `s_contador_ram` conserva su valor.
 *    Deep sleep  : `s_contador_ram` vuelve a cero, pero el contador en
 *                  RTC_DATA_ATTR sigue creciendo.
 *    Hibernacion : incluso el contador RTC_DATA_ATTR vuelve a cero.
 * =====================================================================
 */
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_attr.h"     /* RTC_DATA_ATTR */

#include "config.h"
#include "indicador.h"
#include "energia.h"

static const char *TAG = "main";

/* ------------------------------------------------------------------ */
/*  Variables de estado                                               */
/* ------------------------------------------------------------------ */

/* RTC_DATA_ATTR -> se guarda en la memoria RTC lenta, que permanece
 * alimentada durante el deep sleep. Sobrevive al deep sleep pero NO a
 * la hibernacion ni a un reset por boton EN.                          */
static RTC_DATA_ATTR uint32_t s_ciclos          = 0;
static RTC_DATA_ATTR uint32_t s_despertar_timer = 0;
static RTC_DATA_ATTR uint32_t s_despertar_boton = 0;

/* Variable normal en RAM: se pierde en cada deep sleep. Sirve para
 * evidenciar la diferencia frente al light sleep.                     */
static uint32_t s_contador_ram = 0;

/* ------------------------------------------------------------------ */
/*  Utilidades                                                        */
/* ------------------------------------------------------------------ */

static void imprimir_separador(const char *titulo)
{
    printf("\n");
    printf("========================================================\n");
    printf("  %s\n", titulo);
    printf("========================================================\n");
}

/**
 * @brief Reporta por que se esta ejecutando app_main() y actualiza
 *        las estadisticas acumuladas en memoria RTC.
 */
static void reportar_arranque(void)
{
    causa_despertar_t causa = energia_causa_despertar();

    imprimir_separador("TAREA #6 - GESTION DE ENERGIA EN EL ESP32");

    printf("  Causa del arranque : %s\n", energia_causa_texto(causa));
    printf("  Ciclos completados : %" PRIu32 "\n", s_ciclos);

    switch (causa) {
        case DESPERTAR_TIMER:
            s_despertar_timer++;
            printf("  -> El temporizador RTC agoto su cuenta.\n");
            break;
        case DESPERTAR_BOTON:
            s_despertar_boton++;
            printf("  -> El boton externo (ext0) forzo el despertar.\n");
            break;
        case DESPERTAR_ENCENDIDO:
            printf("  -> Arranque en frio: los contadores RTC estan en cero.\n");
            break;
        default:
            printf("  -> Fuente de despertar no contemplada.\n");
            break;
    }

    printf("  Despertares por timer : %" PRIu32 "\n", s_despertar_timer);
    printf("  Despertares por boton : %" PRIu32 "\n", s_despertar_boton);
    printf("  Contador en RAM       : %" PRIu32 "  <- deberia ser 0 tras deep sleep\n",
           s_contador_ram);
    printf("--------------------------------------------------------\n");
}

/**
 * @brief Fase de trabajo activo.
 *
 * Simula la carga util del sistema: incrementa contadores y publica
 * telemetria por el puerto serial una vez por segundo. Es el estado de
 * mayor consumo (LED encendido + CPU a plena velocidad).
 *
 * @param duracion_ms Duracion de la fase.
 * @param etiqueta    Nombre de la fase para el log.
 * @return true si el boton fue presionado durante la fase.
 */
static bool fase_activa(int duracion_ms, const char *etiqueta)
{
    ESP_LOGI(TAG, "### FASE ACTIVA (%s) durante %d ms ###",
             etiqueta, duracion_ms);
    indicador_set(IND_ACTIVO);

    bool boton = false;
    int  transcurrido = 0;

    while (transcurrido < duracion_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        transcurrido += 100;

        if (energia_boton_presionado()) {
            boton = true;
        }

        /* Telemetria una vez por segundo. */
        if (transcurrido % 1000 == 0) {
            s_contador_ram++;
            printf("  [t=%2d s] trabajando... contador_ram=%" PRIu32
                   "  heap=%" PRIu32 " B\n",
                   transcurrido / 1000,
                   s_contador_ram,
                   esp_get_free_heap_size());
        }
    }

    return boton;
}

/* ------------------------------------------------------------------ */
/*  Programa principal                                                */
/* ------------------------------------------------------------------ */

void app_main(void)
{
    /* 1) Recuperar el control de los pines tras un posible deep sleep. */
    indicador_liberar_hold();

    /* 2) Inicializacion de perifericos. */
    indicador_init();
    energia_init_boton();

    /* 3) Informar por que estamos aqui. */
    reportar_arranque();

    /* Si se desperto con el boton aun presionado, esperar a que lo suelte
     * para no encadenar despertares inmediatos.                          */
    if (energia_boton_presionado()) {
        ESP_LOGW(TAG, "boton aun presionado, esperando a que se suelte...");
        energia_esperar_soltar_boton(3000);
    }

    /* 4) Senal visual de arranque. */
    indicador_parpadeo(IND_ACTIVO, 2, 120);

    /* ---------------------------------------------------------------- */
    /*  FASE 1: trabajo activo prolongado                               */
    /* ---------------------------------------------------------------- */
    fase_activa(T_ACTIVO_LARGO_MS, "previa al light sleep");

    /* ---------------------------------------------------------------- */
    /*  FASE 2: LIGHT SLEEP                                             */
    /* ---------------------------------------------------------------- */
    imprimir_separador("TRANSICION A LIGHT SLEEP");
    printf("  Valor de contador_ram ANTES de dormir : %" PRIu32 "\n",
           s_contador_ram);
    printf("  Consumo esperado en light sleep       : ~0.8 mA\n");
    printf("  Presione el boton para despertar antes de tiempo.\n\n");

    indicador_parpadeo(IND_LIGHT_SLEEP, 3, 150);

    int64_t t_dormido = esp_timer_get_time();
    energia_light_sleep(T_LIGHT_SLEEP_US);
    t_dormido = esp_timer_get_time() - t_dormido;

    imprimir_separador("DE VUELTA DEL LIGHT SLEEP");
    printf("  Tiempo dormido real                   : %" PRId64 " ms\n",
           t_dormido / 1000);
    printf("  Valor de contador_ram DESPUES         : %" PRIu32 "\n",
           s_contador_ram);
    printf("  -> La RAM sobrevivio: la CPU solo se PAUSO.\n");

    /* ---------------------------------------------------------------- */
    /*  FASE 3: trabajo activo corto                                    */
    /* ---------------------------------------------------------------- */
    fase_activa(T_ACTIVO_CORTO_MS, "previa al deep sleep");

    /* ---------------------------------------------------------------- */
    /*  FASE 4: DEEP SLEEP o HIBERNACION                                */
    /* ---------------------------------------------------------------- */
    s_ciclos++;

    if (s_ciclos >= CICLOS_ANTES_HIBERNAR) {
        imprimir_separador("TRANSICION A HIBERNACION");
        printf("  Se completaron %" PRIu32 " ciclos.\n", s_ciclos);
        printf("  Consumo esperado en hibernacion       : ~5 uA\n");
        printf("  Al despertar, los contadores RTC estaran en CERO,\n");
        printf("  porque la memoria RTC tambien se apaga.\n\n");

        indicador_parpadeo(IND_HIBERNACION, 4, 150);
        energia_hibernar(T_DEEP_SLEEP_US);
        /* no retorna */
    }

    imprimir_separador("TRANSICION A DEEP SLEEP");
    printf("  Valor de contador_ram ANTES de dormir : %" PRIu32 "\n",
           s_contador_ram);
    printf("  Consumo esperado en deep sleep        : ~10 uA\n");
    printf("  Al despertar, contador_ram sera 0 pero el contador\n");
    printf("  de ciclos en memoria RTC seguira creciendo.\n");
    printf("  Presione el boton para despertar antes de tiempo.\n\n");

    indicador_parpadeo(IND_DEEP_SLEEP, 3, 150);
    energia_deep_sleep(T_DEEP_SLEEP_US);

    /* Inalcanzable: esp_deep_sleep_start() no retorna. */
}
