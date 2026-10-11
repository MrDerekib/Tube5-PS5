# Tube5

Nombre de la aplicación: **Tube5**. Repositorio público: **Tube5-PS5**.

Aplicación multimedia homebrew nativa para PlayStation 5, diseñada para una
experiencia de salón y preparada para integrar saltos de segmentos mediante
SponsorBlock.

> Estado actual: vídeo local y HTTP LAN, con diez minutos continuos de 1080p
> y controles validados en PS5 Fat FW 13.20. El PC prepara y sirve el contenido
> de YouTube; no hay streaming directo ni bloqueo de anuncios de YouTube.
> Piloto SponsorBlock de DIAG36 validado por reporte del propietario en PS5.
> Biblioteca LAN y corrección de títulos de DIAG35 validadas en consola.
> 4K no validado.

## Prueba actual: BOOT-DIAG-36-SPONSOR

La tarjeta **Biblioteca LAN** carga una lista acotada desde el PC. Cruceta:
seleccionar; X: reproducir; Círculo: volver a la lista; Options: recargarla.
Durante el vídeo, X pausa, izquierda/derecha saltan cinco segundos y Triángulo
abre el selector de posición (X confirma; Círculo cancela).
Se reutilizan los dos clips existentes, sin nuevas descargas.
DIAG35 corrige las minúsculas invisibles en la fuente y muestra 360p para el
clip sintético, sin etiquetarlo como un supuesto ID de vídeo.
Consultar [el piloto LAN](docs/YOUTUBE_TEST.md) para iniciar el servidor.
El [servicio local de URLs](docs/BRIDGE_SERVICE.md) permite añadir vídeos desde
el PC y refrescar la biblioteca sin cambiar la build de consola.
El flujo completo con una URL nueva a 360p está confirmado en PS5.
DIAG36 añade saltos de patrocinio en los vídeos completos y permite ON/OFF
con Options durante reproducción. Consultar [el piloto SponsorBlock](docs/SPONSORBLOCK.md)
para la prueba corta de Veritasium. El propietario confirma que funciona bien
en consola; esta confirmación no equivale a una prueba exhaustiva de todos los controles.
Las secciones siguientes conservan el historial del prototipo.

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

La siguiente fase añade un servidor LAN que expone solo
el clip sintético y permite alternar LOCAL/HTTP con X en Ajustes. La reproducción
local sigue siendo el modo inicial; no hay integración con YouTube todavía.
DIAG13 se bloqueó al abrir HTTP en consola. BOOT-DIAG-14-HTTP adapta las
funciones de resolución y flags de sockets al título nativo, evitando imports
de WebKit en esas llamadas. La prueba de red admite IPv4 numérico; la corrección
en consola evitó la pantalla de crash, pero se quedó esperando en H01.
**BOOT-DIAG-15-HTTP** fuerza/verifica sockets no bloqueantes antes de connect,
añade un límite cooperativo de apertura/probing y traces de conexión sin pausas.
La prueba física de DIAG15 queda validada tras permitir el puerto del servidor
en el firewall del PC: IMG_4260.MOV muestra el clip HTTP completo y regreso al
menú, sin fallos de memoria ni descartes en las muestras revisadas. El usuario
confirma pausa, seeks y reapertura fuera de cámara.
**BOOT-DIAG-16-HTTP** abre/probea la fuente HTTP en un hilo auxiliar, muestra
carga cancelable con Círculo y devuelve al menú ante un error de lectura o
ausencia prolongada de frames. Sin reconexión automática ni rebuffering todavía;
Su prueba física confirma timeout en unos 6 s, cancelación y reproducción de
nuevo tras restaurar el servidor. Sigue pendiente estabilidad prolongada.
Ver [configuración del servidor y prueba HTTP](docs/HTTP_TEST.md).

El [primer piloto con contenido de YouTube](docs/YOUTUBE_TEST.md) prepara un
MP4 privado en el PC y lo sirve por la misma ruta LAN, sin nueva build PS5.
No es streaming directo: se descarga previamente; validación física pendiente.
El MP4 completo dio ENOMEM de lectura en PS5 y crash tras reintentos.
**BOOT-DIAG-17-HTTP** conserva el diagnóstico en el menú y bloquea nuevas
aperturas tras ese ENOMEM hasta reiniciar Tube5. Es una protección y prueba
diagnóstica, no la solución confirmada al vídeo completo. El servidor usa ahora
un extracto de 120 s con los mismos códecs; resultado físico pendiente.

Para actualizar la prueba: cerrar Tube5 y copiar **toda** la carpeta
`dist/PPSA99999/` a `/data/homebrew/PPSA99999/`, sustituyendo la anterior.
Incluye `eboot.bin`, `sce_sys/`, `sce_module/` y `assets/`; no basta con el ejecutable
para una instalación inicial. Si ya tienes la carpeta completa de la build 8 o
9, 10, 11 o 12, la prueba HTTP necesita sustituir `eboot.bin`, `build-label.txt` y
añadir `network-test-url.txt` siguiendo la guía HTTP; el paquete también incluye
los créditos en `THIRD_PARTY_NOTICES.md`.
Si ya está instalada DIAG13–16 con la URL correcta, DIAG17 solo cambia el ejecutable
y la etiqueta.

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

La integración usa datos de [SponsorBlock](https://sponsor.ajay.app/) bajo
[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/), con fuente
y licencia visibles en Ajustes/Acerca de. El código del proyecto sigue GPL.
