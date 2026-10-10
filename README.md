# Tube5

Nombre de la aplicación: **Tube5**. Repositorio público: **Tube5-PS5**.

Aplicación multimedia homebrew nativa para PlayStation 5, diseñada para una
experiencia de salón y preparada para integrar saltos de segmentos mediante
SponsorBlock.

> Estado: prototipo con navegación y vídeo local con audio. Todavía no reproduce
> YouTube ni bloquea anuncios; SponsorBlock está pendiente.

## Prototipo 01.000.001

Menú con vídeo de prueba, ajustes informativos y créditos. El clip sintético de
20 segundos es propio y no requiere Internet. Se reproduce mediante SDL2,
SDL_kitchensink y FFmpeg, con decodificación por software.

- Cruceta: navegar; izquierda/derecha durante el vídeo: saltar 5 segundos.
- X: abrir o pausar/reanudar. Círculo: volver. Options: ajustes fuera del vídeo.

El arranque, menú, cruceta, X, Ajustes, Acerca de y Options están confirmados en
PS5 Fat FW 13.20 con kstuff activo. Cargar Keyboard y usar composición offscreen
con VideoOut nativo eliminaron los fallos iniciales. La reproducción, pausa,
saltos y reapertura pasan las pruebas en PC con sanitizadores. La grabación de
BOOT-DIAG-9 mostraba la primera reproducción negra y un único frame congelado
después de reanudar.
Leer y decodificar paquetes no garantiza que se presenten fotogramas.

BOOT-DIAG-10 consiguió imagen en movimiento desde el primer intento en esa PS5,
pero se bloqueó sobre el segundo 7, precedido por reservas fallidas de unos
983 KB. BOOT-DIAG-11 conserva las mejoras de memoria/dibujo y cambia los frames
intermedios a YUV420 para reducir su tamaño. Impide encolar frames cuya conversión
falló y añade contadores del conversor. La grabación local IMG_4279 confirma una
reproducción completa de 20 segundos y regreso al menú sin crash en esa PS5,
con MEMFAIL en cero cerca del final. Esa build todavía presentaba numerosos
descartes de frames; la prueba de cadencia siguiente usa DIAG12.
El crash anterior todavía no tiene una causa exacta confirmada. No se garantiza
compatibilidad con otros firmwares. Consulta [el diagnóstico de arranque](docs/STARTUP_DIAGNOSTICS.md)
para compilar la variante con puntos de control y registrar la prueba.

BOOT-DIAG-12 elimina la espera adicional de 33 ms en el callback PS5: VideoOut
ya espera al refresco después de presentar. Añade `MS WORK` para distinguir
trabajo del callback del intervalo total y separa los controles del HUD.
No cambia el reloj, el audio ni las colas de DIAG11. La grabación IMG_4280
confirma reproducción, saltos atrás/adelante y regreso al menú sin crash:
las muestras muestran LOOP 16–17 ms, SYNC DROP 0, MEMFAIL 0 y dos seeks
ejecutados sin error. El usuario confirma además pausa/reanudación y reapertura;
la sincronización audible y estabilidad prolongada no quedan validadas.
La build 11 probada queda respaldada localmente en
`.local/boot-diag-11/` (no publicada).

La siguiente prueba, **BOOT-DIAG-13-HTTP**, añade un servidor LAN que expone solo
el clip sintético y permite alternar LOCAL/HTTP con X en Ajustes. La reproducción
local sigue siendo el modo inicial; no hay integración con YouTube todavía.
Ver [configuración del servidor y prueba HTTP](docs/HTTP_TEST.md).

Para actualizar la prueba: cerrar Tube5 y copiar **toda** la carpeta
`dist/PPSA99999/` a `/data/homebrew/PPSA99999/`, sustituyendo la anterior.
Incluye `eboot.bin`, `sce_sys/`, `sce_module/` y `assets/`; no basta con el ejecutable
para una instalación inicial. Si ya tienes la carpeta completa de la build 8 o
9, 10, 11 o 12, la prueba 13 necesita sustituir `eboot.bin`, `build-label.txt` y
añadir `network-test-url.txt` siguiendo la guía HTTP; el paquete también incluye
los créditos en `THIRD_PARTY_NOTICES.md`.

## Primera arquitectura

La aplicación separará interfaz, catálogo, reproductor y el adaptador de
SponsorBlock. Así podremos comprobar cada componente en PS5 sin depender de
una sola fuente de vídeo. Los detalles están en
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Desarrollo local

La plantilla usa C++20, Make, Clang, Ninja y el SDK público PS5 Payload SDK.
El SDK y las dependencias del proyecto se descargan bajo `.deps/` al preparar
el entorno:

```bash
make deps USE_CCACHE=0
PS5_CLANG=clang USE_CCACHE=0 make
```

La compilación genera `dist/PPSA99999/` y un ZIP con la carpeta para el
loader homebrew compatible. `PPSA99999` es una identidad temporal de desarrollo
y debe cambiarse antes de cualquier distribución.

En este host está disponible Clang 22 con el nombre `clang`, no el binario
`clang-18` que busca la plantilla por defecto. Por ello los comandos anteriores
lo seleccionan explícitamente. La compilación ha sido validada localmente, pero
la plantilla sólo declara soporte de Clang 18: antes de publicar un release se
hará una prueba en hardware y, si es posible, con la versión de Clang soportada.
El lint global con clang-format 22 detecta diferencias de formato en ejemplos
heredados de la plantilla; los archivos de este prototipo sí pasan el chequeo
de formato local. CI utiliza clang-format 18.

Pruebas de navegación, framebuffer y herramientas: `USE_CCACHE=0 make test`.
El ensayo del reproductor en PC requiere SDL2, FFmpeg, libass, CMake y Ninja:

```bash
git clone https://github.com/katajakasa/SDL_kitchensink.git .deps/host/SDL_kitchensink
git -C .deps/host/SDL_kitchensink checkout c1bac5287e14122b2cf886628bdb802cf3737d83
git -C .deps/host/SDL_kitchensink apply ../../../tools/patches/kitchensink-video-timing.patch
bash tools/test-host-media.sh
```

Usa la misma corrección de tiempo de vídeo que el port de PacBrew. Comprueba
apertura, decodificación, pausa, seek, fin de clip y reapertura; genera capturas
en `build/`. Las pruebas headless no validan la salida física de audio ni el
mando de PS5. El clip se regenera con `bash tools/generate-test-clip.sh`.

Consulta [el roadmap](docs/ROADMAP.md), incluida la futura opción discreta de
donaciones. Tube5 seguirá siendo gratuita.

## Créditos y licencia

Tube5 parte de
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
licenciado GPL-3.0-or-later. Este repositorio también se publica bajo
GPL-3.0-or-later; consulta [LICENSE](LICENSE) y
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

SponsorBlock ofrece una API pública y exige respetar su atribución y sus
límites de uso. La futura integración incluirá los avisos correspondientes.
