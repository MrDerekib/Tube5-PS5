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

## BOOT-DIAG-7: ejecución real del pipeline

El propietario observa `FRAMES 0`, `BUF 0/2`, `PKT 0/16` de forma persistente.
La cruceta incrementa SEEK, pero no produce salto observable. No se ha confirmado
sonido ni progreso del reloj. Las colas vacías no prueban por sí solas un fallo
de lectura, hilos o decodificación; el consumidor de textura no ha recibido frames.

Sin cambiar el backend, la build 7 cuenta llamadas reales mediante wrappers que
preservan argumentos y retornos. Los contadores son atómicos, sin heap ni avisos
por paquete. Se activan DESPUÉS del probing de la fuente y antes de Play, y se
desactivan después de unir los workers. Así se excluye la lectura de cabeceras.

- `W entrada/setup/salida`: workers que entraron en `SDL_RunThread`, terminaron
  su setup y salieron. No incluye el hilo de audio que empezó antes de Play.
- `READ paquetes/llamadas ERR retorno`: lecturas efectivas de `av_read_frame`;
  si llamadas crece pero paquetes no, el retorno orienta el fallo de lectura.
  Un retorno negativo puede ser EOF normal, no siempre un error fatal.
- `VSEND`: intentos de enviar paquetes al codec de vídeo; `VDEC`: frames
  recibidos correctamente. `ERR envío/recepción` excluye EAGAIN y EOF normales.
- `LOCKFAIL`: errores de bloqueo de mutex SDL; no detecta un bloqueo que nunca
  retorna. `SEEKEXEC`: llamadas efectivas a `avformat_seek_file`, con su retorno.
  Es distinto de SEEK enviados/solicitados de la línea inferior.

Los tests simulan etapas, errores, EAGAIN normal, audio excluido y reinicio por
sesión. La prueba de vídeo real en PC también enlaza los wrappers de FFmpeg y
comprueba paquetes enviados, frames recibidos y seeks realmente ejecutados.
Los hooks internos SDL se verifican en el ejecutable nativo; su comportamiento
en PS5 se ha contrastado con los contadores indicados a continuación.

## BOOT-DIAG-8: memoria de la aplicación

El propietario comunica en la build 7: workers preparados `3/3`,
`READ 45/46 ERR -12`, `VSEND 18 VDEC 1 ERR -1094995529/0`,
`LOCKFAIL 0 SEEKEXEC 0 ERR 0`. No se conoce el tercer contador de workers
(salidas). FFmpeg interpreta `-12` como ENOMEM y `-1094995529` como datos no
válidos. Hubo lectura de paquetes y un frame decodificado, pero ninguno llegó
a subirse a la textura. No se ha confirmado sonido ni progreso del reloj.

Esto orienta hacia asignaciones fallidas, sin demostrar aún su causa: no implica
que falte RAM física en la PS5, ni descarta un problema posterior de conversión.
No se cambia el runtime `libc.prx`, la configuración de hilos ni el formato del clip.

La build 8 reserva el framebuffer RGBA de 1920×1080 en memoria directa nativa,
redondeada a 8 MiB, y crea una superficie SDL que utiliza ese almacenamiento
externo. Antes esos 8.294.400 bytes se reservaban en el heap de SDL. La superficie
se destruye antes de liberar la memoria externa. El backend VideoOut y el dibujo
siguen iguales; esto no configura un heap global ni cambia los allocators.

Los wrappers de `malloc`, `calloc`, `realloc` y `posix_memalign` preservan sus
argumentos y resultados. Sólo cuentan fallos con tamaño solicitado distinto de
cero, sin nuevas reservas ni notificaciones dentro del hook. El HUD añade
`MEMFAIL cantidad tipo bytesB ERR código`: indica la última asignación fallida,
no memoria libre ni consumo total. `ERR` sólo contiene un retorno de error para
`ALIGN`; para los otros allocators es cero porque no se captura errno. Las
instantáneas atómicas pueden mezclar dos fallos concurrentes. Se reinician al
abrir cada clip.

Los tests verifican redondeo, límites, fallo de mapeo, liberación, dirección física
cero válida y paso intacto de los allocators. En PC se prueba además la ruta
offscreen con superficie de memoria externa, reproducción, pausa y seek con
sanitizadores. Tras la prueba en PS5, la lectura y decodificación progresan
(`READ 241/241`, `VDEC 77/77`) y el buffer de salida se llena (`BUF 2/2`), pero
no se sube ningún frame (`FRAMES 0`). `MEMFAIL 1 ALIGN 983168B ERR 12` indica una
asignación fallida de `posix_memalign`; no impide que este clip se decodifique.
La build 9 añade el número de lecturas de frame raw aceptadas/rechazadas (`RAW`),
el último retorno de esa API (`RC`), el estado del player (`S`) y las llamadas,
bytes entregados y errores del callback de audio (`AUD`). KitchenSink rechaza
una lectura raw si no hay frame disponible o aún no corresponde mostrarlo; esos
campos ayudan a distinguir sincronización y entrega de audio. La causa aún no
está confirmada.

## Build con puntos de control

```bash
PS5_CLANG=clang USE_CCACHE=0 make APP_DEFINITIONS=TUBE5_DIAGNOSTICS BUILD_LABEL=BOOT-DIAG-9 \
  APP_WRAP_SYMBOLS="PS5_Keyboard_Init PS5_Keyboard_Open sceVideoOutOpen SDL_CreateRGBSurface_REAL avcodec_open2 SDL_CreateThread_REAL pthread_create SDL_RunThread SDL_SYS_SetupThread SDL_LockMutex_REAL av_read_frame avcodec_send_packet avcodec_receive_frame avformat_seek_file malloc calloc realloc posix_memalign"
```

Cerrar Tube5 y sustituir la carpeta completa `dist/PPSA99999/` en la consola,
incluyendo `sce_module/`. No cambiar los payloads ni el firmware para esta prueba.
Grabar los avisos y comunicar el último código Dxx visible.
Si ya está instalada la build 8 completa, para esta actualización sólo cambian
`eboot.bin` y `build-label.txt`; basta con sustituir esos dos archivos con Tube5
cerrado. Si el vídeo sigue negro, comunicar `MEMFAIL` y las tres líneas del pipeline.

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
