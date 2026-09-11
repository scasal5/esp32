# XPowersLib (vendoreada)

Copia sin modificar de XPowersLib, del repositorio del autor. No es el fork
republicado por terceros en el registro de componentes.

| | |
|---|---|
| Origen | https://github.com/lewisxhe/XPowersLib |
| Tag | `v0.3.3` |
| Commit | `ef40b49af66fe3b1907ab74ca76e10c0dbf69012` |
| Licencia | MIT, ver [`LICENSE`](LICENSE) |

## Que se copio

`CMakeLists.txt`, `Kconfig`, `LICENSE`, `README.md` y `src/`, byte a byte desde
un clone con `core.autocrlf=false`. Es todo lo que usa el componente de ESP-IDF.

Upstream guarda 11 de esos archivos con CRLF y 13 con LF. El
[`.gitattributes`](.gitattributes) de esta carpeta (`* -text`) evita que git
los normalice: el repo y cualquier checkout, en Windows o Linux, tienen los
mismos bytes que el tag.

Quedan afuera `datasheet/` (PDFs de los fabricantes, que no estan bajo la MIT de
la libreria), `examples/`, `Micropython/`, `tools/` y los metadatos de Arduino y
PlatformIO.

## Por que no es una dependencia git

Como dependencia git en `main/idf_component.yml`, el Component Manager clona el
repo y compara el hash del arbol con el `component_hash` de `dependencies.lock`.
XPowersLib no tiene `.gitattributes`: en Windows con `core.autocrlf=true` los
archivos que upstream guarda con LF salen con CRLF, en Linux quedan como estan,
y el hash cambia.

Medido con `idf_component_tools.hash_tools.calculate.hash_dir` sobre `ef40b49`:

| Arbol | Hash |
|---|---|
| Clone en Windows (CRLF) | `876babebaaa444b003bdba5fd7cf58b8c5f216c50f6a29be09fe7fcb998e45b4` |
| Clone con LF (Linux, CI) | `7b257c96e362b297d4802c39155118fbbffa8c63f39904dacd9541495488b079` |

Un lock generado en Windows rompe el build de GitHub Actions con
`The downloaded component "xpowerslib" is corrupted`. Un componente local no
tiene hash que comparar.

## Actualizar

1. `git -c core.autocrlf=false clone https://github.com/lewisxhe/XPowersLib.git`
   y `checkout` del tag nuevo.
2. Reemplazar los archivos de la lista de arriba, sin editarlos.
3. Actualizar tag y commit en esta tabla y en `NOTICE`.
4. Un PR propio: solo la actualizacion, sin otros cambios.
