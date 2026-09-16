# Companion v0

Contrato del companion de la placa: SoftAP de provision WiFi y HTTP de Fondo
para subir la imagen de inicio. Las UIs del SoftAP y de Fondo son **separadas**
(no hay SPA unificada).

El HTML embebido en firmware es la fuente de verdad en v0:

- SoftAP: pagina armada en [`main/wifi_portal.c`](../main/wifi_portal.c)
- Fondo: [`main/fondo_form.html`](../main/fondo_form.html) embebido por CMake

No hay arbol `/web` espejo en este release.

Apagado total: `idf.py menuconfig` → **ws183-os** → **Companion (SoftAP + Fondo HTTP)**
(`CONFIG_WS183_COMPANION`). Con eso en `n` no arranca el portal ni el HTTP de
Fondo; STA, scan y el resto del firmware siguen.

---

## SoftAP · `192.168.4.1`

Se abre desde la card **WiFi** (o `wifiprov <ssid>` por consola). SoftAP abierto
`ws183-XXXX`, un cliente, HTTP + DNS captive en `192.168.4.1`.

| Metodo | Ruta | Body | Respuesta |
|---|---|---|---|
| `GET` | `/` | — | HTML del formulario (o “esperando Si en la placa”) |
| `GET` | `/status` | — | JSON `{"state":"idle\|connecting\|up\|fail","ssid":"..."}` |
| `POST` | `/connect` | **`application/x-www-form-urlencoded`**: `ssid=...&pass=...` | `{"ok":true}` · **415** si Content-Type no es form (p.ej. JSON) · **400** `{"ok":false}` sin ssid/pass · **403** si no hubo Si |

El formulario hace `fetch POST /connect` con `URLSearchParams` / `FormData` y
luego hace poll a `GET /status` hasta `up` o `fail`. **No redirige.**

El SoftAP **no** se baja en el primer `IP_EVENT_STA_GOT_IP`: la UI espera ~10 s
despues de `up` para que el celular vea el resultado, y recien ahi cierra
(o el usuario con BOOT / Cerrar).

### Riesgo SoftAP abierto (aceptado en v0)

El SoftAP es red **abierta** (`WIFI:T:nopass`). Quien se une y gana el Si/No
en la placa envia la clave de la WiFi destino por **HTTP en claro** hacia
`192.168.4.1`. El gate Si/No **no** mitiga sniff en radio. Mitigaciones v0:
un solo cliente, ventana corta, SoftAP que baja al cerrar la UI, y aviso en
la UX. WPA2 en SoftAP queda fuera de v0 (P1/P2).

`GET /connect` y `GET /status` **no** envian CORS generico; solo el endpoint
captive usa `Access-Control-Allow-Origin: *`.


Arranque: SoftAP/HTTP viven en tasks propias (`httpd`, DNS). `app_main` inicia
WiFi **despues** del primer frame LVGL; el portal no bloquea el splash.

---

## Fondo · IP de la STA

Con WiFi conectado, la card **Fondo** levanta HTTP en `http://<IP>/` (solo
mientras la app esta abierta):

| Metodo | Ruta | Body | Respuesta |
|---|---|---|---|
| `GET` | `/` | — | HTML de espera / formulario de subida |
| `GET` | `/status` | — | JSON `{"state":"idle\|wait\|allowed\|denied"}` |
| `POST` | `/upload` | bytes PNG o GIF (hasta 1 MB, 240×284) | JSON `{"ok":true}` o error |

El splash se guarda como `splash.new` → validar → borrar destino → `rename` a
`splash.gif` / `splash.png`. Un fallo no deja un archivo a medias usable; al
boot, si falta el destino y hay un `splash.new` valido, se termina el rename.

---

## Como probar (COM3)

Puerto habitual en Windows: **COM3**. En Linux suele ser `/dev/ttyACM0`.

```bash
# build + flash + monitor
cd firmware
idf.py -p COM3 flash monitor
```

### SoftAP + `/status`

1. En la placa: BOOT → card **WiFi** → esperar el scan → tocar una red con `*`.
2. Escanear el QR `WIFI:` y unirse a `ws183-XXXX`.
3. En la placa: aceptar la MAC (Si). El QR pasa a `http://192.168.4.1/`.
4. Desde el celular (unido al SoftAP) o desde la PC en esa red:

```bash
curl -s http://192.168.4.1/status
# {"state":"idle","ssid":""}  (o connecting/up/fail)

curl -s -X POST http://192.168.4.1/connect \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data-urlencode 'ssid=MiRed' \
  --data-urlencode 'pass=clave'
# {"ok":true}

# poll hasta up o fail
curl -s http://192.168.4.1/status
```

Por consola USB (monitor en COM3), sin tocar Si en la placa:

```
wifiprov MiRed
# cuando el cliente asocie (o desde el harness):
wifiprov allow
wifi
# denied:
# wifiprov deny
```

`wifiprov allow` / `wifiprov deny` llaman a `svc_wifi_prov_allow()` /
`svc_wifi_prov_deny()` (mismo gate que el Si/No de la UI).

### Lab · `CONFIG_WS183_PROV_AUTO_ALLOW`

En `idf.py menuconfig` → **ws183-os** → **Lab: auto-allow SoftAP client**.
Default **n**. Con **y**, al asociarse el primer cliente SoftAP se llama
`svc_wifi_prov_allow()` solo (sin Si en la placa). **Solo lab/harness**; no
usar en builds de produccion. El contrato HTTP (`form-urlencoded`) no cambia.

### Fondo

Con STA ya en IP: card **Fondo** → QR a `http://<IP>/` → Si en la placa →
subir PNG/GIF 240×284 desde el celular.

### Sin companion

```
idf.py menuconfig   # ws183-os → desmarcar Companion
idf.py -p COM3 flash
```

`wifiprov` / Fondo HTTP deben fallar; el splash y STA siguen andando.
