/**
 * @file    indicador.c
 * @brief   Implementacion del indicador de estado por LED RGB.
 */
#include "indicador.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "indicador";

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

void indicador_init(void)
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

    ESP_LOGI(TAG, "LED RGB en GPIO%d(R) GPIO%d(V) GPIO%d(A)",
             LED_ROJO_GPIO, LED_VERDE_GPIO, LED_AZUL_GPIO);
}

void indicador_set(indicador_estado_t estado)
{
    color_t c = color_de(estado);
    gpio_set_level((gpio_num_t)LED_ROJO_GPIO,  c.r ? 1 : 0);
    gpio_set_level((gpio_num_t)LED_VERDE_GPIO, c.v ? 1 : 0);
    gpio_set_level((gpio_num_t)LED_AZUL_GPIO,  c.a ? 1 : 0);
}

void indicador_apagar(void)
{
    indicador_set(IND_APAGADO);
}

void indicador_parpadeo(indicador_estado_t estado, int veces, int ms)
{
    for (int i = 0; i < veces; i++) {
        indicador_set(estado);
        vTaskDelay(pdMS_TO_TICKS(ms));
        indicador_apagar();
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

void indicador_liberar_hold(void)
{
    /* Los tres pines del LED son RTC_GPIO, por lo que podrian haber quedado
     * congelados. Se libera el hold para recuperar el control normal.     */
    rtc_gpio_hold_dis((gpio_num_t)LED_ROJO_GPIO);
    rtc_gpio_hold_dis((gpio_num_t)LED_VERDE_GPIO);
    rtc_gpio_hold_dis((gpio_num_t)LED_AZUL_GPIO);
}
