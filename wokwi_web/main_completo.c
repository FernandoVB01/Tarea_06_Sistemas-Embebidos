/**
 * =====================================================================
 *  TAREA #6 - SISTEMAS EMBEBIDOS (FIEC - ESPOL)
 *  Ejercicio 1: Gestion de Energia en el ESP32
 *
 *  Autor: Fernando Velez
 * =====================================================================
 *
 *  VERSION DE UN SOLO ARCHIVO, para pegar en Wokwi web (wokwi.com).
 *
 *  Es el mismo codigo que src/, con config.h, indicador.c y energia.c
 *  fundidos aqui dentro. Se hizo asi porque en el navegador crear ocho
 *  archivos es tedioso y el paso que mas se olvida (main/CMakeLists.txt)
 *  hace fallar el enlazado con errores confusos. Con un solo archivo ese
 *  problema desaparece.
 *
 *  La fuente de verdad sigue siendo src/. Si cambias algo alli, hay que
 *  reflejarlo aqui a mano.
 *
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
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_attr.h"     /* RTC_DATA_ATTR */

/* =================================================================== */
/*  CONFIGURACION  (era config.h)                                      */
/* =================================================================== */

/* --- LED RGB de estado (catodo comun) ----------------------------- */
#define LED_ROJO_GPIO   25   /* RTC_GPIO6  - indica DEEP SLEEP  */
#define LED_VERDE_GPIO  26   /* RTC_GPIO7  - indica FASE ACTIVA */
#define LED_AZUL_GPIO   27   /* RTC_GPIO17 - indica LIGHT SLEEP */

/* --- Boton de despertar externo (ext0) ---------------------------- */
/*  DEBE ser un GPIO con funcion RTC: durante el deep sleep el dominio
 *  digital esta apagado y solo el subsistema RTC sigue vigilando.
 *  GPIO33 = RTC_GPIO8 segun la Tabla 2-1 del datasheet.              */
#define BOTON_WAKE_GPIO 33

/* --- Tiempos del ciclo -------------------------------------------- */
#define T_ACTIVO_LARGO_MS  8000        /* trabajo antes del light sleep */
#define T_ACTIVO_CORTO_MS  4000        /* trabajo antes del deep sleep  */
#define T_LIGHT_SLEEP_US   5000000ULL  /* 5 s dormido en light sleep    */
#define T_DEEP_SLEEP_US    10000000ULL /* 10 s dormido en deep sleep    */

/* --- Ciclos completos antes de pasar a hibernacion ---------------- */
#define CICLOS_ANTES_HIBERNAR 3

#define BOTON ((gpio_num_t)BOTON_WAKE_GPIO)

/* =================================================================== */
/*  INDICADOR LED RGB  (era indicador.c / indicador.h)                 */
/* =================================================================== */
/*
 *      VERDE      -> fase activa (consumo maximo)
 *      AZUL       -> a punto de entrar en light sleep
 *      ROJO       -> a punto de entrar en deep sleep
 *      MAGENTA    -> a punto de entrar en hibernacion
 *      APAGADO    -> el sistema esta durmiendo
 *
 *  Los LEDs se apagan SIEMPRE antes de dormir. Un LED encendido consume
 *  miliamperios; el deep sleep esta en microamperios. Dejarlo prendido
 *  anularia por completo el ahorro que se busca demostrar.
 */

static const char *TAG_IND = "indicador";

typedef enum {
    IND_APAGADO = 0,
    IND_ACTIVO,        /* verde   */
    IND_LIGHT_SLEEP,   /* azul    */
    IND_DEEP_SLEEP,    /* rojo    */
    IND_HIBERNACION,   /* magenta */
} indicador_estado_t;

/* Composicion de color: {rojo, verde, azul} para cada estado. */
typedef struct { bool r, v, a; } color_t;

static color_t color_de(indicador_estado_t e)
{
    switch (e) {
        case IND_ACTIVO:       return (color_t){ false, true,  false }; /* verde   */
        case IND_LIGHT_SLEEP:  return (color_t){ false, false, true  }; /* azul    */
        case IND_DEEP_SLEEP:   return (color_t){ true,  false, false }; /* rojo    */
        case IND_HIBERNACION:  return (color_t){ true,  false, true  }; /* magenta */
        case IND_APAGADO:
        default:               return (color_t){ false, false, false };
    }
}

static void indicador_set(indicador_estado_t estado)
{
    color_t c = color_de(estado);
    gpio_set_level((gpio_num_t)LED_ROJO_GPIO,  c.r ? 1 : 0);
    gpio_set_level((gpio_num_t)LED_VERDE_GPIO, c.v ? 1 : 0);
    gpio_set_level((gpio_num_t)LED_AZUL_GPIO,  c.a ? 1 : 0);
}

static void indicador_apagar(void)
{
    indicador_set(IND_APAGADO);
}

static void indicador_init(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_ROJO_GPIO)  |
                        (1ULL << LED_VERDE_GPIO) |
                        (1ULL << LED_AZUL_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    indicador_apagar();

    ESP_LOGI(TAG_IND, "LED RGB en GPIO%d(R) GPIO%d(V) GPIO%d(A)",
             LED_ROJO_GPIO, LED_VERDE_GPIO, LED_AZUL_GPIO);
}

static void indicador_parpadeo(indicador_estado_t estado, int veces, int ms)
{
    for (int i = 0; i < veces; i++) {
        indicador_set(estado);
        vTaskDelay(pdMS_TO_TICKS(ms));
        indicador_apagar();
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

/**
 * Libera el "hold" de los pines RTC tras un deep sleep.
 *
 * Si antes de dormir se congelo el estado de un pin con rtc_gpio_hold_en(),
 * al despertar queda bloqueado hasta liberarlo explicitamente. Se llama al
 * arrancar para garantizar que los LEDs vuelvan a ser controlables.
 */
static void indicador_liberar_hold(void)
{
    rtc_gpio_hold_dis((gpio_num_t)LED_ROJO_GPIO);
    rtc_gpio_hold_dis((gpio_num_t)LED_VERDE_GPIO);
    rtc_gpio_hold_dis((gpio_num_t)LED_AZUL_GPIO);
}

/* =================================================================== */
/*  GESTION DE ENERGIA  (era energia.c / energia.h)                    */
/* =================================================================== */
/*
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
 *                 despierta por temporizador RTC. Consumo: 5 uA.
 *
 *  (Cifras de consumo tomadas de la Tabla 4-2 del datasheet ESP32 v5.2.)
 */

static const char *TAG_ENE = "energia";

typedef enum {
    DESPERTAR_ENCENDIDO = 0,  /* arranque en frio / reset   */
    DESPERTAR_TIMER,          /* vencio el temporizador RTC */
    DESPERTAR_BOTON,          /* pin externo ext0           */
    DESPERTAR_OTRO,
} causa_despertar_t;

static void energia_init_boton(void)
{
    /* El pin debe tener funcion RTC para servir como fuente ext0.
     * GPIO33 = RTC_GPIO8 segun la Tabla 2-1 del datasheet.           */
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOTON_WAKE_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* reposo = nivel alto  */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    ESP_LOGI(TAG_ENE, "boton de despertar en GPIO%d (activo en bajo)",
             BOTON_WAKE_GPIO);
}

static bool energia_boton_presionado(void)
{
    return gpio_get_level(BOTON) == 0;   /* activo en bajo */
}

static void energia_esperar_soltar_boton(int timeout_ms)
{
    int transcurrido = 0;
    while (energia_boton_presionado() && transcurrido < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(20));
        transcurrido += 20;
    }
    /* Pequena espera antirrebote tras soltar. */
    vTaskDelay(pdMS_TO_TICKS(50));
}

static causa_despertar_t energia_causa_despertar(void)
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: return DESPERTAR_TIMER;
        case ESP_SLEEP_WAKEUP_EXT0:  return DESPERTAR_BOTON;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            /* No hubo sleep previo: es un arranque en frio o un reset. */
            return DESPERTAR_ENCENDIDO;
        default:
            return DESPERTAR_OTRO;
    }
}

static const char *energia_causa_texto(causa_despertar_t causa)
{
    switch (causa) {
        case DESPERTAR_ENCENDIDO: return "ENCENDIDO/RESET";
        case DESPERTAR_TIMER:     return "TEMPORIZADOR RTC";
        case DESPERTAR_BOTON:     return "BOTON EXTERNO (ext0)";
        default:                  return "OTRA FUENTE";
    }
}

/**
 * Habilita temporizador + ext0 como fuentes de despertar.
 *
 * ext0 despierta cuando el pin llega a nivel 0 (boton presionado). Se
 * mantiene el pull-up durante el sueno, de lo contrario el pin flotaria
 * y el chip despertaria por ruido electrico.
 */
static void configurar_fuentes(uint64_t us_timer)
{
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(us_timer));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BOTON, 0));

    /* Sin este pull-up el pin queda flotante durante el sueno. */
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(BOTON));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(BOTON));
}

/**
 * Vacia la FIFO de transmision del UART0 antes de dormir.
 *
 * Si el chip se duerme con bytes pendientes, esos bytes salen recortados o
 * con la velocidad equivocada y el monitor serial muestra basura. Es uno
 * de los errores mas comunes al implementar modos de sueno.
 */
static void vaciar_uart(void)
{
    uart_wait_tx_idle_polling(UART_NUM_0);
}

static causa_despertar_t energia_light_sleep(uint64_t us_timer)
{
    ESP_LOGI(TAG_ENE, "--> entrando en LIGHT SLEEP (%llu ms o boton)",
             us_timer / 1000ULL);

    indicador_apagar();          /* el LED anularia el ahorro */
    configurar_fuentes(us_timer);
    vaciar_uart();

    /* La CPU se pausa aqui. Al despertar, la ejecucion CONTINUA en la
     * linea siguiente y toda la RAM conserva su contenido.            */
    esp_light_sleep_start();

    causa_despertar_t causa = energia_causa_despertar();
    ESP_LOGI(TAG_ENE, "<-- despierto de LIGHT SLEEP por: %s",
             energia_causa_texto(causa));

    return causa;
}

static void energia_deep_sleep(uint64_t us_timer)
{
    ESP_LOGI(TAG_ENE, "--> entrando en DEEP SLEEP (%llu ms o boton)",
             us_timer / 1000ULL);
    ESP_LOGI(TAG_ENE, "    el chip REINICIARA al despertar");

    indicador_apagar();
    configurar_fuentes(us_timer);
    vaciar_uart();

    /* Esta llamada no retorna: el chip se apaga y luego reinicia. */
    esp_deep_sleep_start();
}

static void energia_hibernar(uint64_t us_timer)
{
    ESP_LOGI(TAG_ENE, "--> entrando en HIBERNACION (%llu ms)",
             us_timer / 1000ULL);
    ESP_LOGI(TAG_ENE, "    se perderan las variables RTC_DATA_ATTR");

    indicador_apagar();

    /* Solo temporizador: ext0 necesita el dominio RTC_PERIPH, que se
     * apaga justo debajo. Habilitar ambos seria contradictorio.      */
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(us_timer));

    /* La hibernacion se consigue apagando explicitamente los dominios
     * de potencia que el deep sleep normal mantiene encendidos.      */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,   ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL,         ESP_PD_OPTION_OFF);

    vaciar_uart();
    esp_deep_sleep_start();
}

/* =================================================================== */
/*  PROGRAMA PRINCIPAL  (era main.c)                                   */
/* =================================================================== */

static const char *TAG = "main";

/* RTC_DATA_ATTR -> se guarda en la memoria RTC lenta, que permanece
 * alimentada durante el deep sleep. Sobrevive al deep sleep pero NO a
 * la hibernacion ni a un reset por boton EN.                          */
static RTC_DATA_ATTR uint32_t s_ciclos          = 0;
static RTC_DATA_ATTR uint32_t s_despertar_timer = 0;
static RTC_DATA_ATTR uint32_t s_despertar_boton = 0;

/* Variable normal en RAM: se pierde en cada deep sleep. Sirve para
 * evidenciar la diferencia frente al light sleep.                     */
static uint32_t s_contador_ram = 0;

static void imprimir_separador(const char *titulo)
{
    printf("\n");
    printf("========================================================\n");
    printf("  %s\n", titulo);
    printf("========================================================\n");
}

/**
 * Reporta por que se esta ejecutando app_main() y actualiza las
 * estadisticas acumuladas en memoria RTC.
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
 * Fase de trabajo activo.
 *
 * Simula la carga util del sistema: incrementa contadores y publica
 * telemetria por el puerto serial una vez por segundo. Es el estado de
 * mayor consumo (LED encendido + CPU a plena velocidad).
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
