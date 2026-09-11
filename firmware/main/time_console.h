#pragma once

/*
 * Consola por USB-Serial-JTAG (esp_console REPL) con dos comandos:
 *
 *   settime <epoch_utc>   pone la hora del sistema y la graba en el RTC
 *   time                  muestra la hora local (UTC-3) y la del RTC
 *
 * scripts/set_time.ps1 manda settime con la hora de la PC.
 */
void time_console_start(void);
