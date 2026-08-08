/**
 * @file    energia.c
 * @brief   Implementacion de los modos de bajo consumo del ESP32.
 */
#include "energia.h"
#include "indicador.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "esp_sleep.h"
#include "esp_log.h"

static const char *TAG = "energia";

#define BOTON ((gpio_num_t)BOTON_WAKE_GPIO)

/* ================================================================== */
/*  Boton de despertar externo                                        */
/* ================================================================== */

void energia_init_boton(void)
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

    ESP_LOGI(TAG, "boton de despertar en GPIO%d (activo en bajo)",
             BOTON_WAKE_GPIO);
}

bool energia_boton_presionado(void)
{
    return gpio_get_level(BOTON) == 0;   /* activo en bajo */
}

void energia_esperar_soltar_boton(int timeout_ms)
{
    int transcurrido = 0;
    while (energia_boton_presionado() && transcurrido < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(20));
        transcurrido += 20;
    }
    /* Pequeña espera antirrebote tras soltar. */
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* ================================================================== */
/*  Causa de despertar                                                */
/* ================================================================== */

causa_despertar_t energia_causa_despertar(void)
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

const char *energia_causa_texto(causa_despertar_t causa)
{
    switch (causa) {
        case DESPERTAR_ENCENDIDO: return "ENCENDIDO/RESET";
        case DESPERTAR_TIMER:     return "TEMPORIZADOR RTC";
        case DESPERTAR_BOTON:     return "BOTON EXTERNO (ext0)";
        default:                  return "OTRA FUENTE";
    }
}

/* ================================================================== */
/*  Configuracion comun de las fuentes de despertar                   */
/* ================================================================== */

/**
 * Habilita temporizador + ext0 como fuentes de despertar.
 *
 * ext0 despierta cuando el pin llega a nivel 0 (boton presionado). Se
 * mantiene el pull-up durante el sueño, de lo contrario el pin flotaria
 * y el chip despertaria por ruido electrico.
 */
static void configurar_fuentes(uint64_t us_timer)
{
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(us_timer));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BOTON, 0));

    /* Sin este pull-up el pin queda flotante durante el sueño. */
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(BOTON));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(BOTON));
}

/**
 * Vacia la FIFO de transmision del UART0 antes de dormir.
 *
 * Si el chip se duerme con bytes pendientes, esos bytes salen recortados o
 * con la velocidad equivocada y el monitor serial muestra basura. Es uno
 * de los errores mas comunes al implementar modos de sueño.
 */
static void vaciar_uart(void)
{
    uart_wait_tx_idle_polling(UART_NUM_0);
}

/* ================================================================== */
/*  LIGHT SLEEP                                                       */
/* ================================================================== */

causa_despertar_t energia_light_sleep(uint64_t us_timer)
{
    ESP_LOGI(TAG, "--> entrando en LIGHT SLEEP (%llu ms o boton)",
             us_timer / 1000ULL);

    indicador_apagar();          /* el LED anularia el ahorro */
    configurar_fuentes(us_timer);
    vaciar_uart();

    /* La CPU se pausa aqui. Al despertar, la ejecucion CONTINUA en la
     * linea siguiente y toda la RAM conserva su contenido.            */
    esp_light_sleep_start();

    causa_despertar_t causa = energia_causa_despertar();
    ESP_LOGI(TAG, "<-- despierto de LIGHT SLEEP por: %s",
             energia_causa_texto(causa));

    return causa;
}

/* ================================================================== */
/*  DEEP SLEEP                                                        */
/* ================================================================== */

void energia_deep_sleep(uint64_t us_timer)
{
    ESP_LOGI(TAG, "--> entrando en DEEP SLEEP (%llu ms o boton)",
             us_timer / 1000ULL);
    ESP_LOGI(TAG, "    el chip REINICIARA al despertar");

    indicador_apagar();
    configurar_fuentes(us_timer);
    vaciar_uart();

    /* Esta llamada no retorna: el chip se apaga y luego reinicia. */
    esp_deep_sleep_start();
}

/* ================================================================== */
/*  HIBERNACION                                                       */
/* ================================================================== */

void energia_hibernar(uint64_t us_timer)
{
    ESP_LOGI(TAG, "--> entrando en HIBERNACION (%llu ms)",
             us_timer / 1000ULL);
    ESP_LOGI(TAG, "    se perderan las variables RTC_DATA_ATTR");

    indicador_apagar();

    /* Solo temporizador: ext0 necesita el dominio RTC_PERIPH, que se
     * apaga justo debajo. Habilitar ambos seria contradictorio.      */
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(us_timer));

    /* La hibernacion se consigue apagando explicitamente los dominios
     * de potencia que el deep sleep normal mantiene encendidos.      */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,     ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM,   ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM,   ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL,           ESP_PD_OPTION_OFF);

    vaciar_uart();
    esp_deep_sleep_start();
}
