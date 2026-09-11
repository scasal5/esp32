#include "time_console.h"
#include "board_rtc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "esp_console.h"
#include "esp_log.h"

static const char *TAG = "time_console";

/* Rango aceptado para settime: desde 2024-01-01 (evita dejar la placa en 1970
   por un argumento mal escrito) hasta el ultimo segundo que el PCF85063A
   puede guardar, 2099-12-31 23:59:59 UTC. */
#define EPOCH_MIN 1704067200LL
#define EPOCH_MAX 4102444799LL

static void print_now(void)
{
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    printf("hora local (UTC-3): %s epoch=%lld\n", buf, (long long)now);
}

static int cmd_settime(int argc, char **argv)
{
    if (argc != 2) {
        printf("uso: settime <epoch_utc>\n");
        return 1;
    }

    char *end = NULL;
    long long epoch = strtoll(argv[1], &end, 10);
    if (end == argv[1] || epoch < EPOCH_MIN || epoch > EPOCH_MAX) {
        printf("settime: epoch fuera de rango (%lld..%lld)\n", EPOCH_MIN, EPOCH_MAX);
        return 1;
    }

    const struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    esp_err_t err = board_rtc_set((time_t)epoch);
    if (err != ESP_OK) {
        printf("settime: sistema actualizado, RTC fallo: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("settime ok\n");
    print_now();
    return 0;
}

static int cmd_time(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    print_now();

    time_t rtc;
    esp_err_t err = board_rtc_get(&rtc);
    if (err == ESP_OK) {
        printf("RTC (UTC) epoch=%lld\n", (long long)rtc);
    } else {
        printf("RTC: %s\n", esp_err_to_name(err));
    }
    return 0;
}

void time_console_start(void)
{
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "ws183>";

    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    esp_console_repl_t *repl = NULL;
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sin consola: %s", esp_err_to_name(err));
        return;
    }

    const esp_console_cmd_t settime = {
        .command = "settime",
        .help = "Pone la hora del sistema y del RTC",
        .hint = "<epoch_utc>",
        .func = &cmd_settime,
    };
    const esp_console_cmd_t time_cmd = {
        .command = "time",
        .help = "Muestra la hora local (UTC-3) y la del RTC",
        .hint = NULL,
        .func = &cmd_time,
    };
    esp_console_register_help_command();
    esp_console_cmd_register(&settime);
    esp_console_cmd_register(&time_cmd);

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no arranco la consola: %s", esp_err_to_name(err));
    }
}
