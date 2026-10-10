# Diagnóstico de arranque en PS5

La build 01.000.001 compiló y pasó los tests en PC, pero el propietario informa
de un fallo en PS5 Fat FW 13.20: pantalla de carga de la plantilla, aviso de
bloqueo antes de pausar kstuff y CE-108255-1. Los juegos del SSD externo siguen
arrancando tras actualizar los payloads. No se ha confirmado la causa.

La app añade SDL, FFmpeg y dependencias de PacBrew respecto a la base que sí
arrancaba. Enlazar sus símbolos no prueba su disponibilidad en un título nativo.
Hay imports de Keyboard, ImeDialog y PosixForWebKit a revisar.

## Resultado BOOT-DIAG-1 y siguiente prueba

El propietario observa D00 a D05 y después el bloqueo, sin D06. Esto acota
el fallo a `SDL_SetMainReady()` o `SDL_Init(SDL_INIT_VIDEO)`; no demuestra por
sí solo qué llamada interna falla. El archivo de SDL del sysroot fijado confirma
que `PS5_CreateDevice` llama a `PS5_Keyboard_Init` y `PS5_Keyboard_Open` antes de
inicializar VideoOut. Estas llamadas no cargan el módulo Keyboard.

BOOT-DIAG-2 carga y comprueba el módulo nativo Keyboard `0x0106` antes de SDL.
El ID y la obligación de cargarlo antes de inicializar están documentados en
[la investigación de input nativo](https://github.com/blackbearreloaded/ps5-native-gamepad-input-research).
Con BOOT-DIAG-2 el propietario confirma que desaparece el crash inicial:
se alcanza D09, SDL devuelve "Couldn't find matching render driver" y la app
permanece en negro por el bucle controlado de error. Esto apoya la corrección
del módulo Keyboard, sin atribuirle todavía todos los posibles fallos.

## BOOT-DIAG-3: composición offscreen y VideoOut nativo

El archivo `SDL_ps5video.c.o` del sysroot fijado contiene un callback de
framebuffer que crea una superficie pero no escribe los parámetros de salida
`format`, `pixels` y `pitch`. La creación de un renderer software asociado a
esa ventana no es una ruta viable para este prototipo.

Tube5 crea ahora su superficie RGBA de 1080p y un renderer con
[`SDL_CreateSoftwareRenderer`](https://wiki.libsdl.org/SDL2/SDL_CreateSoftwareRenderer),
sin inicializar el subsistema de vídeo SDL en PS5. Conserva SDL para eventos,
mando, composición y audio. La salida usa el VideoOut nativo de la plantilla,
con copia acotada de la superficie lineal a sus buffers tiled. No abre un segundo
VideoOut mediante SDL. Las pruebas de vídeo en PC también usan superficie sin
ventana ni driver de vídeo.

El propietario confirma D10, seguido de `UI TEXTURE` y
`Parameter 'surface' is invalid`, sin llegar al primer frame. El oso y el portal
son la pantalla de carga heredada: VideoOut todavía no se ha iniciado. La
superficie principal y el renderer se han creado; no se conoce aún la causa de
que falle la superficie interna de la textura.

## BOOT-DIAG-4: menú directamente sobre la superficie

La ruta offscreen pinta el menú directamente en `output->pixels`, comprobando
formato, dimensiones, pitch y bloqueo de la superficie. Elimina la textura de
interfaz y su buffer de subida, aproximadamente 16 MiB de asignaciones. No
demuestra que el fallo previo fuera falta de memoria ni valida las texturas de
vídeo en PS5; el reproductor conserva su textura.

El archivo `SDL_render_sw.c.o` del sysroot fijado confirma que `SW_CreateTexture`
llama a `SDL_CreateRGBSurface_REAL` y luego a setters incluso si recibió `nullptr`.
Esos setters sobrescriben el error original. Un wrapper de diagnóstico informa
del error del constructor antes de que se pierda, con ancho, alto y bits en hex.
La prueba en PC simula ese fallo y comprueba la conservación del mensaje y los
argumentos. También comprueba menú directo y vídeo superpuesto sin ventana SDL.

## BOOT-DIAG-5: apertura del decodificador

En BOOT-DIAG-4 el propietario confirma menú, navegación con cruceta, entrada en
Ajustes y Acerca de y funcionamiento de Options. Al abrir Vídeo de prueba,
SDL_kitchensink informa `Unable to open codec for stream 0`. Se ha pasado la
apertura de fuente, selección de streams, búsqueda del decodificador y creación
de su contexto, pero `avcodec_open2` falla. Todavía no se llega a la textura de
vídeo ni a la apertura del dispositivo de audio del reproductor.

BOOT-DIAG-5 fija `KIT_HINT_THREAD_COUNT=1` antes de crear el reproductor, en lugar
de autodetectar el número de hilos. Es una prueba conservadora para reducir
recursos e inicialización de workers de FFmpeg, no una causa confirmada ni una
garantía de reproducción. Los hilos de demux/audio/vídeo de Kitchensink siguen
activos. Las pruebas en PC usan la misma configuración.

Un wrapper de `avcodec_open2` conserva su retorno negativo y la descripción de
`av_strerror` antes de que Kitchensink lo sustituya por su mensaje genérico.
Muestra codec, error y cantidad de hilos solicitada por notificación; también
incluye el detalle al principio del mensaje amarillo del menú. Se borra antes
de cada intento para no mostrar errores antiguos. Los tests simulan éxito,
fallo y reinicio, comprobando que los argumentos y el retorno no se modifican.

## BOOT-DIAG-6: frames, hilos y saltos

Con BOOT-DIAG-5 el propietario ya puede entrar en la pantalla del reproductor y
usar pausa, pero el área de vídeo permanece negra y los saltos de 5 segundos no
funcionan según su prueba. Esto confirma que se supera la apertura de codecs,
textura y dispositivo de audio, no que se hayan decodificado frames ni que haya
salida física de sonido. Sonido y progreso del contador quedan por confirmar.

La build 6 usa `Kit_LockPlayerVideoRawFrame` para copiar explícitamente RGBA con
`SDL_UpdateTexture`, comprobando formato/tamaño/pitch y su resultado; mantiene
la textura anterior en pausa y fuerza mezcla `NONE`. Antes, el helper de
Kitchensink ignoraba los errores de subida de textura, y Tube5 ignoraba los de
dibujo. Ahora los errores de subida, dibujo y seek aparecen en amarillo.

Se diagnostica `SDL_CreateThread_REAL` y el retorno de `pthread_create`.
Kitchensink marca el estado de los workers como activo incluso si SDL devuelve
un hilo nulo. Si sucede durante el arranque del reproductor, la build de
diagnóstico rechaza la apertura en lugar de anunciar reproducción inexistente.
No se atribuye todavía el negro a estos hilos: los wrappers sólo registran y
preservan el resultado original, sin sustituir la implementación.

La pantalla de reproducción muestra `FRAMES` (subidas exitosas), `BUF` (frames
decodificados en cola), `PKT` (paquetes en cola) y `SEEK enviados/solicitados`.
Un envío no garantiza que el demuxer haya ejecutado el salto: a2 puede descartar
peticiones pendientes. Anotar esos valores tras unos segundos y después de
pulsar izquierda/derecha permite localizar el punto detenido. Un aviso
`Tube5 VIDEO: primer frame subido` confirma la primera copia RGBA exitosa.

Los tests de PC comprueban imagen real no negra, contadores de subida, pausa,
saltos hacia delante y atrás DURANTE reproducción, seek en pausa y EOF. Los
tests de hilos inyectan fallo y verifican los argumentos, resultado y conservación
del primer error.

## Build con puntos de control

```bash
PS5_CLANG=clang USE_CCACHE=0 make APP_DEFINITIONS=TUBE5_DIAGNOSTICS BUILD_LABEL=BOOT-DIAG-6 \
  APP_WRAP_SYMBOLS="PS5_Keyboard_Init PS5_Keyboard_Open sceVideoOutOpen SDL_CreateRGBSurface_REAL avcodec_open2 SDL_CreateThread_REAL pthread_create"
```

Cerrar Tube5 y sustituir la carpeta completa `dist/PPSA99999/` en la consola,
incluyendo `sce_module/`. No cambiar los payloads ni el firmware para esta prueba.
Grabar los avisos y comunicar el último código Dxx visible.

| Último aviso | La siguiente operación pendiente |
| --- | --- |
| Ninguno | Entrada del ejecutable, imports, loader o envío de la primera notificación |
| D00 | Lectura de parámetros del proceso y `_init_env` |
| D01 | Registro de destructores y constructores estáticos |
| D02 | Entrada en `main` |
| D03 | Lectura de etiqueta de build |
| D04 | UserService |
| D04K | Carga/comprobación del módulo Keyboard; un retorno distinto de cero detiene el arranque |
| D05 | `SDL_SetMainReady()` |
| D05A | `SDL_Init(SDL_INIT_EVENTS)` en la ruta nativa offscreen |
| D05K1 | Inicialización de Keyboard; el aviso de retorno contiene su resultado |
| D05K2 | Apertura de Keyboard; el aviso de retorno contiene su resultado |
| D05V | Apertura de VideoOut; el aviso de retorno contiene su resultado |
| D06 | SDL audio |
| D07 | SDL mando |
| D08 | Superficie RGBA offscreen |
| D09 | Renderer software directo sobre esa superficie |
| D10 | Tamaño lógico, validación del framebuffer y conexión del mando (sin textura UI en PS5) |
| D11 | SDL_kitchensink |
| D12 | Eventos, dibujo y presentación |
| D12V | Configuración y primer flip de VideoOut nativo |
| D13 | Primer frame presentado; evaluar el fallo posterior |
| TEXTURE: fallo original | Constructor de superficie para vídeo; anotar el mensaje siguiente y sus dimensiones |
| CODEC: apertura fallida | Apertura de codec FFmpeg; anotar codec y mensaje `FFmpeg -N: ...` |
| THREAD: no se pudo crear | Worker SDL fallido; anotar nombre, error SDL y retorno de pthread_create |
| VIDEO: primer frame subido | Copia RGBA a textura correcta; todavía hay que confirmar imagen visible |

Los avisos se pueden encolar: el último visible es una pista, no una traza de
crash. Si SDL devuelve un error, se muestran etapa y detalle y la app permanece
viva para leerlos. El hook anterior al runtime no usa heap, stdio ni SDL.

Para una build normal, omitir `APP_DEFINITIONS` y `BUILD_LABEL`: los avisos de
diagnóstico quedan desactivados. La compatibilidad de esta build sigue pendiente
de la prueba en la consola.
