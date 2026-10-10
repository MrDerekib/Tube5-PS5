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

## BOOT-DIAG-10: rendimiento y decisiones de sincronización

La grabación local de 61 segundos de la build 9 muestra una primera reproducción
negra hasta 00:20 (`FR 0`, `RAW 0/64`, `BUF 2/2`). Al reanudar sin salir del player,
se muestra un único frame del clip, timestamp 00:00:04.200/frame 126. La interfaz
avanza de 00:13 a 00:20 pero la imagen y `FR 1` permanecen iguales. En esa sesión
`MEMFAIL 0` y los errores de lectura/codec son cero. No demuestra reproducción
continua ni audio correcto: Kitchensink puede devolver bytes de silencio.

El código fijado de Kitchensink descarta frames con más de 50 ms de retraso;
si termina de descartar una cola antes de que llegue un frame a tiempo, devuelve
1 y Tube5 conserva negro o la última textura. Esto es una hipótesis compatible
con la grabación, todavía no una causa confirmada.

La build 10 corrige costes identificados en nuestra ruta:

- El framebuffer de trabajo de SDL utiliza memoria directa tipo 12 con protección
  `0x33`, en vez de tipo 3 write-combined. SDL y el tiler leen ese almacenamiento
  desde CPU; los buffers finales VideoOut conservan tipo 3. El tipo 12 y su
  comportamiento con caché se contrastaron con
  [PS5_Vulkan, commit 5b5e4fc](https://github.com/mihawk-99/PS5_Vulkan/blob/5b5e4fc2d80fdb67a4f61ba9e6a8a424026bb8a5/driver/ps5vk_private.h)
  y sus [mediciones de memoria](https://github.com/mihawk-99/PS5_Vulkan/blob/5b5e4fc2d80fdb67a4f61ba9e6a8a424026bb8a5/docs/HARDWARE_FINDINGS.md).
- La copia tiled escribe en orden de direcciones físicas dentro de cada tile.
  Conserva el mapeo de los píxeles visibles y rellena el padding inferior en negro.
- Se elimina el checkpoint bloqueante de 700 ms al subir el primer frame;
  `FR` sigue confirmando las subidas sin detener la reproducción.
- Tras abrir el clip se descartan pulsaciones de activación acumuladas y se
  esperan 350 ms antes de aceptar otra pausa. No corrige por sí solo un mando
  que pierda pulsaciones entre lecturas del backend.

El HUD añade `MS LOOP/UI/VIDEO/COPY`: milisegundos del último ciclo, composición
UI, lectura/subida/dibujo de vídeo y copia tiled. LOOP incluye presentación,
espera de refresco y pacing; los valores se dibujan con una iteración de retraso.

`SYNC PTS/CLOCK` muestra en milisegundos el último candidato de vídeo y el reloj
usado durante la lectura raw, no necesariamente el frame presentado. `PTS -1`
significa que aún no se observó un timestamp válido. PacBrew añade 100 ms al
primer candidato dentro del sincronizador; el HUD muestra el PTS del archivo.
`DROP` cuenta frames retirados dentro de esa lectura; `WAIT`, lecturas canceladas
sin consumir el frame; `EMPTY`, intentos que no obtuvieron nada de la cola.
Los wrappers preservan argumentos, retornos y reglas de sincronización. No
acceden a layouts de structs privados; filtran el hilo de lectura de vídeo.

Se revisó también
[BFplayer ad3bdc9](https://github.com/ItsBlurf/BFplayer/tree/ad3bdc9d7742606913701dac035f29e2ab49b080):
su fork repara pausa/reanudación del reloj, consumo parcial de audio y utiliza
audio como reloj maestro. No se ha trasladado ese fork completo a Tube5 ni se
atribuye nuestro fallo al mismo bug de audio.

Validación: compilación nativa y contenedor correctos; el test de copia comprueba
los 2.073.600 píxeles visibles contra el mapeo anterior. Pasan los siete tests de
canvas/navegación, los mocks de memoria y diagnóstico, y la reproducción real
en PC con sanitizadores: imagen, pausa, saltos, EOF, reapertura y limpieza.
También compila y genera preview el modo offscreen sin diagnóstico. El ELF
nativo confirma que las llamadas internas de sincronización alcanzan los
wrappers; los contadores específicos de esta ruta aún deben observarse en PS5.
La compatibilidad y el rendimiento de esta modificación requieren la prueba
en PS5 FW 13.20.
La grabación privada queda excluida de Git y del paquete de la aplicación.

## BOOT-DIAG-11: frames YUV y conversión fallida

La siguiente grabación local (18,235 s, unos 19,6 MB) confirma que la build 10
presenta imagen en movimiento desde el primer intento. El último frame visible
del clip es 00:00:07.033/frame 211; a continuación aparece CE-108255-1 y el aviso
de bloqueo antes de pausar kstuff. No hay backtrace, así que no se conoce la
llamada exacta que termina el proceso.

Justo antes se observan `MS LOOP 50 UI 2 VIDEO 1 COPY 5`, `FR 115`, `RAW 115/167`,
`BUF 2/2`, `PKT 16/16`, `READ 594/594`, `VSEND 215 VDEC 215 ERR 0/0`,
`LOCKFAIL 0`, `SEEK 0/0`, `SYNC PTS 7033 CLOCK 7168 DROP 97 WAIT 4 EMPTY 48`.
La copia ya no es el cuello de botella observado antes. El frame permanece cerca
del reloj, aunque se descartan frames entre presentaciones. `MEMFAIL` pasa de
cero al inicio a `19 ALIGN 983168B ERR 12`: hay reservas fallidas antes del crash.
No demuestra fuga, falta de RAM física, ni que toda reserva fallida provenga del
conversor; el tamaño encaja con un buffer RGBA padded de 640×384 más margen.

La build 11 solicita `SDL_PIXELFORMAT_IYUV` a Kitchensink y sube explícitamente
los planos Y/U/V con `SDL_UpdateYUVTexture`. a2 no reconoce ese alias en su
tabla de formatos; un wrapper lo asocia a `AV_PIX_FMT_YUV420P` y conserva los
demás formatos sin cambios, igual que la corrección en el fork de BFplayer.
Valida los tres punteros y sus
pitches, mantiene el frame bloqueado hasta completar la subida y conserva la
última textura en pausa. La cola ya no necesita copias RGBA completas: a igual
resolución YUV420 utiliza aproximadamente 1,5 bytes por píxel frente a 4 en RGBA.
El framebuffer de trabajo y VideoOut siguen siendo RGBA; SDL software hace la
conversión final al dibujar. Esto reduce almacenamiento intermedio, no elimina
toda conversión ni configura un nuevo heap. Se contrastó esta elección con
[la ruta SDL de BFplayer ad3bdc9](https://github.com/ItsBlurf/BFplayer/blob/ad3bdc9d7742606913701dac035f29e2ab49b080/src/main.cpp).

El código fijado de Kitchensink a2 ignora el retorno de `sws_scale_frame`.
Un wrapper conserva ese retorno y registra el destino que falló. Si Kitchensink
intenta escribir ese mismo frame en la cola, el guard devuelve false; el código
existente entonces libera sus referencias. Los frames de audio y las escrituras
normales conservan su resultado original. Un contexto de conversión nulo se
rechaza sin llamar al scaler. No se cambia el algoritmo interno de FFmpeg ni se
promete evitar fallos que ocurran dentro de él antes de devolver un resultado.

`YUV SCALE/FAIL/DROP/ERR` muestra conversiones correctas, fallos, frames que el
guard impidió encolar y último retorno negativo. SCALE no equivale a FR.
El guard se añade automáticamente desde `tools/build.sh` para el reproductor
raíz, también sin diagnóstico; no se aplica a los ejemplos independientes de
la plantilla. La prueba de PC usa los mismos wrappers de conversión y extiende
la reproducción inicial continua a unos diez segundos antes de probar pausa,
saltos, EOF y reapertura. Los mocks inyectan ENOMEM, contexto nulo, recuperación
y fallos de la propia cola.

Prueba física de build 11: la grabación local privada IMG_4279.MOV (23,635 s)
confirma imagen en movimiento durante el clip de 20 segundos y regreso al menú
sin crash en PS5 Fat FW 13.20. Cerca del final se observa `MEMFAIL 0 NONE 0B
ERR 0`, `MS LOOP 50 UI 2 VIDEO 7 COPY 4`, errores de lectura y decodificación
en cero, y numerosos descartes en SYNC. El hito valida una reproducción local
completa, no estabilidad prolongada, 30 fps sostenidos ni sincronización audible.
La duración del bucle ronda 50 ms en esa muestra; queda investigar cadencia y
descartes. El HUD inferior se solapa con los controles y dificulta leer YUV SCALE.
Pausa, seek y reapertura en consola siguen pendientes de una prueba específica.

Validación de build 11: pasan los mocks de formato/ENOMEM/contexto nulo y las
pruebas de reproducción YUV con AddressSanitizer/UndefinedBehaviorSanitizer,
incluyendo más de ocho segundos continuos y más de cien subidas antes de la
primera pausa. Pasan también seeks, EOF, reapertura y limpieza; el preview
offscreen compila. Se inspeccionó la captura de vídeo para confirmar imagen y
colores, y el ELF nativo confirma que creación/decodificación llaman a los
wrappers de formato, scaler y escritura. El FSELF valida su integridad.

## BOOT-DIAG-12: una sola espera de presentación

La ruta nativa ejecutaba `SDL_Delay(33 - elapsed)` al terminar el callback y
después `run_frames` vaciaba caché, enviaba el flip y esperaba con
`sceVideoOutWaitVblank`. Son dos esperas consecutivas, compatibles con el
intervalo de 50 ms observado. La build 12 elimina únicamente el delay del
callback PS5; la espera y alternancia de buffers de VideoOut se conservan.
El host mantiene su límite de 33 ms porque no usa ese bucle nativo.

El HUD añade `MS WORK`, tiempo del callback desde eventos hasta copia a tiled,
sin incluir el flush/flip/vblank externo. `MS LOOP` sigue midiendo el intervalo
entre callbacks; su diferencia incluye presentación y planificación, no es una
medición exclusiva de vblank. Los controles pasan a y=1040 para no tapar SYNC
ni YUV SCALE. No se aumenta la tolerancia a frames tardíos para ocultar descartes.

Se revisaron el lector de vídeo, reloj y lector de audio del Kitchensink a2
fijado y los cambios equivalentes de BFplayer ad3bdc9. a2 aplica +0,1 s solo al
primer candidato de vídeo, no a los siguientes tras descartar; pausa no congela
el reloj compartido y resume reinicia su base. El lector de audio consulta
`crop_bottom` después de `av_frame_unref`, un orden que BFplayer corrige.
Son aspectos a tratar con pruebas específicas, no causas demostradas de todos
los descartes de esta grabación. DIAG12 no modifica esas rutas: primero se
compara la cadencia con DIAG11 manteniendo el mismo clip y resto del pipeline.

Antes de construir DIAG12 se respaldaron el eboot, etiqueta y ZIP de DIAG11 en
`.local/boot-diag-11/`. El eboot de referencia conserva SHA-256
`d774978bf00a8f94660288c7809f226564c8f5290201a1d0592f7a94f1aadfa6`.
Prueba física de DIAG12: IMG_4280.MOV (24,770 s, privada y excluida de Git)
muestra reproducción, un salto hacia atrás, otro hacia adelante y regreso al
menú sin crash. A los 11 s de grabación, antes de los saltos: `MS LOOP 17 WORK
14 UI 3 VIDEO 6 COPY 5`, `MEMFAIL 0`, `FR 296`, `RAW 296/598`, `SYNC PTS 9833
CLOCK 9980 DROP 0 WAIT 301 EMPTY 0`, `YUV SCALE 299 FAIL 0 DROP 0 ERR 0`.
A los 19 s, tras ambos saltos: `LOOP 16 WORK 14`, `SEEK 2/2`, `SEEKEXEC 2 ERR 0`,
`FR 525`, `SYNC PTS 15800 CLOCK 15883 DROP 0 WAIT 551 EMPTY 0` y conversión
sin fallos. A los 22 s, `FR 615`, `DROP 0`, `MEMFAIL 0`, lectura/decodificación
sin errores y ambos seeks ejecutados. WAIT no es descarte: el lector conserva
el frame hasta su instante de presentación. Estas muestras respaldan la mejora
de cadencia sin aumentar tolerancias ni alterar el reloj.
El usuario confirma además pausa/reanudación y reapertura con la misma build;
esas operaciones no aparecen en este vídeo. No se afirma estabilidad prolongada,
sincronización audible ni rendimiento equivalente a mayor resolución.

## BOOT-DIAG-13-HTTP: misma reproducción, fuente de red

Añade elección LOCAL/HTTP en Ajustes y URL configurable en
`/app0/network-test-url.txt`. Por defecto mantiene LOCAL. Inicializa Kitchensink
con KIT_INIT_NETWORK y limita esperas por operación HTTP a 5 s mediante un
wrapper de avformat_open_input. No altera sincronización, conversión ni cadencia
de DIAG12. Servidor y procedimiento: [HTTP_TEST.md](HTTP_TEST.md).
DIAG12 queda respaldada localmente antes de construir esta variante. Su prueba
física falló al abrir HTTP mientras LOCAL seguía funcionando.

## BOOT-DIAG-14-HTTP: adaptador de red nativo

Se confirmó en el SDK que getaddrinfo/freeaddrinfo/gai_strerror enlazaban a
libScePosixForWebKit y que tcp_open los invocaba incluso con IP numérica. Se
adaptaron las funciones de resolución al caso LAN IPv4 numérico y fcntl al
SO_NBIO de PS5, siguiendo el adaptador de la propia plantilla. También se
evitan los imports strcasestr/isatty. La revisión del ELF confirma que esas
funciones ya no están indefinidas; fcntl original sigue importado desde libkernel
para conservar llamadas a archivos. Quedan imports de directorios de otras
rutas de las bibliotecas: no se afirma que toda la app esté libre de WebKit.
Detalles, límites y checkpoints H01/H02: [HTTP_TEST.md](HTTP_TEST.md).
La causa exacta del crash anterior y el resultado físico de DIAG14 siguen
pendientes de confirmación.

## BOOT-DIAG-15-HTTP: conexión no bloqueante verificada y plazo de apertura

El usuario reportó que DIAG14 permaneció en H01 y tuvo que cerrarla a la fuerza,
sin la pantalla de crash anterior. El servidor responde desde PC, sin petición
HTTP registrada de PS5 para ese intento; esto no localiza por sí solo la llamada.
DIAG15 fuerza/verifica SO_NBIO antes de connect y devuelve error si no puede
asegurarlo, incluso si fcntl había devuelto éxito. Añade AVIOInterruptCB con un
plazo cooperativo de 8 s para apertura/probing y conserva el timeout por operación.
Ese callback no puede cancelar una syscall bloqueada; la corrección física está
pendiente. H03/H10/H11/H12 son traces sin usleep, para localizar la fase y retorno
de connect. Detalles y pruebas de servidor que no responde: [HTTP_TEST.md](HTTP_TEST.md).

La prueba física de DIAG15 inicialmente devolvió timeout: UFW bloqueaba el puerto
8080 del PC. Tras añadir una excepción limitada a la PS5, sin cambiar la build,
IMG_4260.MOV confirma reproducción HTTP completa y retorno al menú. El usuario
confirma los controles y reapertura fuera de cámara. No demuestra que el crash
de DIAG13 o el bloqueo de DIAG14 tuvieran la misma causa.

## BOOT-DIAG-16-HTTP: apertura auxiliar y recuperación manual

HTTP abre/probea la fuente en un hilo SDL auxiliar y conserva presentación SDL
en el hilo principal. Muestra carga animada y permite pedir cancelación con
Círculo; solo une el hilo en el bucle de UI cuando ya publicó su finalización.
El cierre final pide interrupción a FFmpeg antes de unir los lectores. Añade
propagación de errores de lectura HTTP, detección de final prematuro y watchdog
de 8 s sin frames (excluye pausa y reinicia tras seek/resume). No añade reconexión
automática. LOCAL conserva su apertura anterior. Casos y límites: [HTTP_TEST.md](HTTP_TEST.md).
Prueba física de esta variante pendiente; DIAG15 respaldada localmente.

Después, el usuario confirmó timeout en unos 6 s y cancelación con el servidor
que retiene cabeceras, y reproducción de nuevo al restaurar el servidor normal.

## BOOT-DIAG-17-HTTP: diagnóstico y protección de lectura ENOMEM

El primer MP4 de YouTube preparado en PC abre (H02=0) pero devuelve ENOMEM de
lectura HTTP. El usuario reporta crash normalmente al tercer intento. Aún no
hay backtrace ni datos de la asignación nativa fallida, y el smoke PC pasa.
DIAG17 no cambia allocator, runtime, límites de probing ni códecs: muestra
MEMFAIL y READ también en el menú tras el error y bloquea nuevas aperturas
tras ese ENOMEM hasta reiniciar la app. LOCAL tampoco se abre en esa sesión;
timeouts y conexiones rechazadas conservan su recuperación anterior.
La prueba host inyecta ENOMEM tras subir frames y comprueba cierre, bloqueo de
reintentos y recuperación con instancia nueva. No reproduce el agotamiento
nativo ni demuestra la causa del crash. Se prueba además un extracto de los
primeros 120 s sin recodificar; ver [YOUTUBE_TEST.md](YOUTUBE_TEST.md).
DIAG16 queda respaldada con eboot/etiqueta en `.local/boot-diag-16/`.

## BOOT-DIAG-18-HTTP: conservación de muestras de audio

El usuario confirma reproducción del extracto de 120 s, pero con audio que
cruje/distorsiona. El lector de SDL_kitchensink fijado descarta el resto de cada
paquete tras una lectura parcial: `av_frame_unref(current)` borra `crop_bottom`
antes de decidir si debe retirar el paquete. Cancelar la lectura tampoco copia
el contador modificado al frame de la cola. Una regresión host reproduce la
pérdida de 90 muestras estéreo al leer 1024 de un paquete de 1114 muestras.

DIAG18 sustituye solamente ese lector mediante wrapper: mueve el frame fuera
de la cola, conserva su referencia entre callbacks y ajusta el PTS según los
bytes consumidos. La callback rellena la salida con cuantos bloques necesite;
un bloque corto no introduce automáticamente silencio. No se amplían buffers
ni se reserva otro almacén PCM. Un reset con SDL pausado elimina el fragmento
pendiente antes de seek/cierre. Se mantiene el contrato S16 estéreo/48 kHz.
El ABI privado se limita a la versión fijada; se comprueba con los headers host
y los offsets del archivo nativo (userdata 0x28, current 0x18, queue 0x30).

`AUD ... U<N> E<N>` muestra callbacks/bytes PCM, callbacks con salida incompleta
y último error. Algunos huecos al arrancar o saltar no prueban un fallo; interesa
si `U` crece continuamente durante reproducción normal. La prueba PCM verifica
byte a byte fragmentos y bloques consecutivos, espera por PTS, descarte tardío,
alineación y reset. Los smoke usan audio dummy: la calidad sonora y el efecto
sobre los crujidos siguen pendientes de prueba en PS5. No se da por resuelto
el ENOMEM del vídeo completo de 28 minutos.

Con Tube5 cerrada, sustituir solo `eboot.bin` y `build-label.txt` de
`dist/PPSA99999/` en `/data/homebrew/PPSA99999/`. Mantener URL/payloads. Comparar
audio HTTP primero sin saltos y después pausa/seek/reapertura, con volumen
moderado. Anotar si desaparecen los crujidos y si U sube constantemente.

Validación PC: regresión PCM y batería media con ASan/UBSan/LSan, smoke HTTP
del extracto real (apertura, reproducción inicial, pausa, seek a EOF y reapertura),
`make test` y build nativa con FSELF válido. El smoke no reproduce los 120 s
enteros de forma continua. `make lint` sigue fallando por formato en archivos
previos de self-update, no modificados aquí; el formato de los archivos de
esta corrección y la sintaxis shell pasan. SHA-256 del eboot candidato:
`6909590ba99e0e1ede2ca859e1f97ac0b78af48e4b656004112c5b8d8c6ea4d5`.

## BOOT-DIAG-25-HTTP: buffers grandes de FFmpeg en memoria directa

El propietario confirma apertura y reproducción correcta del extracto de
10 minutos con DIAG23. No se considera una validación de diez minutos continuos.
El completo vuelve a fallar con DIAG24. La captura privada IMG_4282.HEIC muestra
`MEMFAIL 16 ALIGN 247119B ERR 12`, `W 3/3/3 READ 45/45 ERR 0`, `FMEM BEFORE OPEN
963 MiB`, `PLAYING AUDIO 0 VIDEO 0`, `FIRSTFAIL ALIGN 368768B A64` y
`FFALLOC MALLOC 368768B A0 REL 0x1172532`.

Con el ELF exacto de DIAG24, FFALLOC se resuelve a `av_buffer_alloc+0x11`
(llamada a av_malloc). El primer caller alineado es `av_malloc+0x30`: retorno
0x118f761, REL 0x118a1f1. La fuente bitmap de la app omitía '+', 'x' y las letras
hexadecimales minúsculas; ese segundo offset se reconstruye con la llamada
verificada en el ELF y los dígitos visibles, no con una lectura íntegra de la
foto. DIAG25 imprime hexadecimales en mayúsculas y omite el signo positivo.

El primer buffer rechazado pide 368.768 bytes, no los 247.119 del último fallo.
Se localiza una reserva AVBuffer en FFmpeg; no demuestra qué caller de
av_buffer_alloc la solicitó ni un límite exacto del heap. La memoria flexible
se midió ANTES de abrir, no durante el fallo. No se atribuye el problema a RAM
física agotada ni a que el archivo se cargue entero en memoria.

DIAG25 intercepta las llamadas externas a `av_buffer_alloc`/`av_buffer_allocz`
para tamaños de 128 KiB a 2 MiB. Usa memoria directa CPU cached tipo 12,
protección 0x33, alineada/redondeada a páginas de 16 KiB. Conserva la alineación
del framebuffer principal a 2 MiB. Hay 32 slots, como máximo 64 MiB simultáneos.
`av_buffer_create` adopta el almacenamiento con una callback que lo desmapea y
libera cuando FFmpeg suelta su última referencia. Nunca se pasa esa dirección
a free/realloc de libc. El API AVBuffer copia al crecer buffers con callbacks
personalizadas. Si falla la reserva nativa o no quedan slots, se prueba el
allocator original; si falla la creación de metadatos, se libera la reserva.
Un error de liberación pone el slot en cuarentena para evitar reutilizarlo.

Esta es una corrección candidata, no estabilidad confirmada en PS5. El wrap no
intercepta llamadas resueltas internamente en el mismo objeto de FFmpeg; por
eso no sustituye todos sus allocators. El ELF confirma que la ruta de vídeo de
av_frame_get_buffer llama al wrapper nuevo. El runtime libc.prx no se modifica.
El caso de heap privado agotado documentado por
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan/blob/5b5e4fc2d80fdb67a4f61ba9e6a8a424026bb8a5/docs/RADV_PHASE.md)
sirve de antecedente, no de prueba de la causa en Tube5.

`DBUF vivos/32 KIB ALLOC total FAIL total ERR código` muestra reservas directas
actuales, número de buffers creados y fallos de la vía nativa. Los totales son
acumulados desde el arranque de la app. FAIL puede crecer sin un error de
reproducción si funciona el allocator original. Tras cerrar, vivos/KIB deben
volver a cero si todas las referencias se liberaron y las syscalls tuvieron éxito.

Validación focalizada de PC: redondeo de 16 KiB, errores de reserva/mapeo y
liberación, limpieza y reutilización, vida de referencias AVBuffer reales,
relleno cero, realloc por copia, límite de 32 slots, fallback al original,
fallo de creación de metadatos y cuatro workers concurrentes. Build nativa y
FSELF válidos. Las pruebas usan syscalls nativas simuladas y no confirman
rendimiento ni reproducción en PS5. Eboot y ELF exacto respaldados en
`.local/boot-diag-25/`. Eboot DIAG25 SHA-256:
`4daac4db6d866bb5ee84d1fcd0a80dfe06f7e0b55dc595944f4788cc853bd57a`.

Con Tube5 cerrada, copiar `dist/PPSA99999/` a la consola y verificar DIAG25.
El servidor ya sirve el vídeo completo en la URL habitual. Abrir una vez: si
falla, fotografiar DBUF, FIRSTFAIL, FFALLOC y MEMFAIL; no repetir. Si reproduce,
observar primero 30–60 s sin saltos antes de probar controles y reapertura.

## BOOT-DIAG-24-HTTP: localizar el origen sin recorrer la pila

DIAG23 falla con el MP4 completo y conserva los contadores anteriores más
`AUDIO 0 VIDEO 0`. El propietario confirma que el extracto de 120 s sí abre;
no se ha confirmado una reproducción continua de sus 120 s. Se prepara ahora
un extracto de 600 s sin recodificar para probar primero con la misma DIAG23.
Así se compara el archivo servido manteniendo la app instalada.

DIAG24 es una candidata adicional: preserva el primer fallo de reserva en una
instantánea publicada atómicamente (`FIRSTFAIL`, tamaño, alineación y `REL`).
Los wrappers opcionales de `av_malloc`/`av_mallocz` conservan además el primer
fallo observado de esos allocators y su caller (`FFALLOC`). El caller directo
de posix_memalign puede ser solo av_malloc; el registro exterior permite
identificar su solicitante cuando la llamada pasa por el wrapper. Las llamadas
resueltas internamente en el mismo objeto de la biblioteca pueden no pasar por
ese wrapper: `FFALLOC NONE` no descarta un fallo dentro de FFmpeg.

Se lee únicamente la dirección de retorno inmediata, sin recorrer la pila,
usar dladdr, TLS ni reservar memoria desde los hooks. Los retornos originales
se preservan. REL es la diferencia firmada entre ese PC y
`__wrap_posix_memalign`; permite corregir ASLR usando el ELF exacto de la build:

```bash
python3 tools/resolve-allocation-caller.py --elf .local/boot-diag-24/llvm-pie.elf +0xVALOR
# Para un REL negativo:
python3 tools/resolve-allocation-caller.py --elf .local/boot-diag-24/llvm-pie.elf -- -0xVALOR
```

El script resta un byte al PC de retorno para localizar la llamada y usa los
símbolos del ELF nativo previo a la conversión. El resultado identifica función
y desplazamiento; sin información DWARF puede no incluir archivo/línea. No
usar el ELF de otra build. FIRSTFAIL puede diferir de MEMFAIL, que conserva el
último fallo. FFALLOC es una instantánea independiente, no una correlación
garantizada con FIRSTFAIL si fallan varios workers a la vez.

Pruebas focalizadas de PC: paso intacto de los allocators, conservación del
primer fallo frente a fallos posteriores, reinicio por sesión, tamaños cero,
alineación y resolución de offsets positivos/negativos y límites de símbolos.
Build nativa y FSELF válidos; DIAG23 y su ELF respaldados en `.local/boot-diag-23/`.
Eboot DIAG24 SHA-256: `d17ea736f667ab53212af80fabcd9474de6708150b36ced971d4527488087599`.

## BOOT-DIAG-23-HTTP: conservar la primera salida de audio/vídeo

En DIAG22 el propietario confirma que `ALLOC FAIL STAGE PLAYING` aparece y que
los demás contadores siguen iguales. `PLAYING` significa que la llamada
`Kit_PlayerPlay` ya retornó; no confirma que el decodificador haya producido un
frame ni que SDL lo haya mostrado. DIAG23 conserva dos indicadores atómicos
desde el inicio de cada apertura: `AUDIO 1` tras recibir bytes de audio y
`VIDEO 1` tras subir correctamente el primer frame a la textura. No se borran al
cerrar el reproductor después del fallo.

En una única prueba, `AUDIO 0 VIDEO 0` significa que la reserva fallida ocurrió
antes de observar salida de cualquiera de las rutas; `AUDIO 1 VIDEO 0` confirma
entrega de audio pero no subida de vídeo; `VIDEO 1` confirma al menos una subida
correcta. Ninguna combinación identifica por sí sola la llamada que falló, y
`AUDIO 0` no prueba que el decoder de audio no haya trabajado. No repetir si
aparece ENOMEM.

## BOOT-DIAG-22-HTTP: localizar la fase de la reserva fallida

DIAG21 se probó en PS5 Fat 13.20 y añadió `FMEM BEFORE OPEN 936 MiB` a los datos
previos: `MEMFAIL 16 ALIGN 247119B ERR 12`, `W 3/3/3`, `READ 45/45 ERR 0`. Los
936 MiB libres son consistentes con que el kernel aplicó el presupuesto de
1 GiB; no es un valor compatible con los aproximadamente 413 MiB libres que la
referencia registra con el presupuesto por defecto de 448 MiB. El ajuste sí se
cargó y no resolvió el fallo, así que no se debe seguir aumentando la memoria.

DIAG22 registra la fase activa cuando falla una asignación y la muestra como
`ALLOC FAIL STAGE ...` junto a MEMFAIL. Los contextos son `HTTP_OPEN`,
`HTTP_STREAM_SCAN`, `PLAYER_CREATE`, `TEXTURE_CREATE`, `AUDIO_OPEN` y `PLAYING`.
Esto localiza el subsistema, aunque no identifica aún la función exacta que
pidió los 247119 bytes. `dladdr` no proporciona información de módulos en el
adaptador nativo, así que la fase es una pista fiable y acotada para el próximo
paso, no una traza simbólica.

Si vuelve a fallar, anotar juntos `FMEM BEFORE OPEN`, `ALLOC FAIL STAGE`,
`MEMFAIL` y `READ`; DIAG23 añade `AUDIO`/`VIDEO` a la línea de fase. No repetir
intentos.

## BOOT-DIAG-21-HTTP: medir la memoria flexible efectiva

DIAG20 se copió como carpeta completa y se probó en la consola, pero el error
se repite con los mismos datos: `MEMFAIL 16 ALIGN 247119B ERR 12`, `W 3/3/3`,
`READ 45/45 ERR 0`. No hubo avance observable. La coincidencia no permite saber
si el kernel aplicó el nuevo presupuesto o si 1 GiB no bastó.

DIAG21 conserva el ajuste de `kernel.flexibleMemorySize` y consulta
`sceKernelAvailableFlexibleMemorySize` al iniciar cada apertura. Tras un fallo,
la UI mostrará `FMEM BEFORE OPEN N MiB`; alrededor de 989 MiB es compatible con
el presupuesto de 1 GiB, mientras que alrededor de 413 MiB apunta al valor por
defecto de 448 MiB. Son cantidades disponibles, no el tamaño configurado exacto.
Si aparece `FMEM QUERY ERR`, anotar también ese código. La consulta se hace una
vez al abrir, no en cada frame.

El dato decidirá el siguiente paso: si marca el rango alto y falla igual,
dejamos de tocar el presupuesto y aislamos qué asignación de 247119 bytes falla;
si marca el rango bajo, comprobamos la publicación/remontaje del `param.json`.
Probar solo una vez, con Tube5 completamente cerrada antes de copiar.
Build nativa y FSELF válidas; import de memoria flexible presente en el ELF.
SHA-256 del eboot DIAG22: `2b10660bb3f3fcd7bb91c76bb24e368261046f7d5b60f9579d4b3378aec4d86d`.

## BOOT-DIAG-20-HTTP: aumentar la memoria flexible del título

DIAG19 se instaló en PS5 Fat 13.20, pero la apertura del MP4 completo sigue
fallando antes de reproducir: `MEMORY: ENOMEM`, `MEMFAIL 16 ALIGN 247119B ERR
12`, `W 3/3/3`, `READ 45/45 ERR 0`. Las lecturas terminan sin error de red; el
dato nuevo sigue apuntando a una reserva local fallida. El tamaño de la reserva
no identifica al llamador y no prueba que se agotara la memoria flexible.

La revisión de `sce_sys/param.json` encontró que Tube5 no pedía un presupuesto
flexible explícito. La plantilla de referencia documenta el valor por defecto
como 448 MiB y la opción de solicitar hasta 1 GiB mediante
`kernel.flexibleMemorySize`; ese presupuesto se toma de memoria directa, no
aumenta la memoria total. DIAG20 solicita 1 GiB para dar margen a FFmpeg,
SDL_kitchensink y las asignaciones alineadas durante la apertura. Es una
hipótesis comprobable, no una causa confirmada: si el error continúa, la próxima
captura deberá separar fallo durante el escaneo de fuente y durante la creación
del player/codec antes de ajustar otros límites.

La build conserva el código DIAG19; el único cambio funcional es el presupuesto
del título. La prueba posterior no aportó una mejora. Para esa prueba había que
copiar `sce_sys/param.json` a la carpeta de la consola, además de `eboot.bin` y
`build-label.txt`, siempre con
Tube5 completamente cerrada. Probar una apertura del vídeo y, si llega a entrar,
30–60 segundos. No insistir si vuelve a marcar ENOMEM.

## BOOT-DIAG-19-HTTP: presupuesto de probing y colas

Prueba física DIAG18: el extracto reproduce con mejora audible reportada;
IMG_4281 muestra reproducción y U0/E0/MEMFAIL0 en las muestras revisadas. El
usuario confirma avances, retrocesos y salida. Queda un pequeño clic al saltar,
aplazado como pulido. No se pudo escuchar directamente la pista de la grabación.

Al servir el archivo completo se detuvo el servidor mientras la sesión anterior
seguía abierta; el usuario reporta crash de esa sesión. Esa interrupción es un
factor de confusión, no prueba la causa del crash. Después de reabrir la app,
el archivo completo falla de nuevo: HTTP READ ENOMEM, MEMFAIL 118 ALIGN 368768B
ERR12, W3/3, READ142/144 ERR-12. El tamaño es compatible con un framebuffer YUV
acolchado, pero el contador no identifica la pila ni mide memoria libre.

DIAG19 limita el probing HTTP a 5 MiB / 5 s de contenido / 2500 paquetes en
`avformat_find_stream_info`, después de que `_ScanSource` lo suba a INT_MAX.
Aplicarlo solo en las opciones de apertura no serviría: la biblioteca las
sobrescribe antes del scan. Estas opciones públicas se documentan en
[FFmpeg 7 AVFormatContext](https://ffmpeg.org/doxygen/7.0/structAVFormatContext.html).
No son límites del heap ni de las tablas de muestras/índices del MP4.

La cola PCM se reduce de 64 a 8 frames y la cola AAC de 64 a 16 paquetes,
conservando dos frames de vídeo y sus 16 paquetes. En a2, a partir de 16 bloques
PCM en cola el FIFO puede agrupar lotes de hasta unos 8192 samples antes de
emitir: permanecer por debajo evita ese crecimiento durante la acumulación.
No se cambia el allocator, libc.prx, códecs, vídeo, URL ni payloads. El getter
de ENOMEM alineado conserva una bandera hasta reset: la UI cierra/bloquea en
su siguiente tick aunque el fallo haya ocurrido en un decoder antes de READ.
La apertura también rechaza una fuente o decoder que ya haya marcado ENOMEM;
los errores de apertura conservan el bloqueo cuando esa bandera está activa.
EINVAL no activa ese bloqueo. No garantiza evitar todos los crashes internos.

Se prueban los límites después del sobrescrito de `_ScanSource`, fuentes
no rastreadas intactas, hint de colas, bandera ENOMEM/EINVAL y cierre/bloqueo/
instancia nueva mediante inyección host de allocator y de READ. La regresión
PCM sigue activa. Es una corrección candidata: no demuestra la causa exacta,
que haya una fuga, ni que el vídeo completo funcione en PS5.
DIAG18 queda respaldada en `.local/boot-diag-18/`. Eboot DIAG19 SHA-256:
`49ade867575647106373c6b058f09cbb08dd15a2ba3a41fb8fe2e0536d0aae40`.

Validación PC: batería media con ASan/UBSan/LSan y smoke HTTP del MP4 completo
(unos diez segundos iniciales, controles, seek a EOF y reapertura); no equivale
a 28 minutos continuos ni reproduce el allocator PS5. Build nativa/FSELF válida.
Formato de los fuentes modificados y sintaxis shell pasan. El lint global
conserva los problemas previos de formato de self-update, fuera de esta tarea.

Cerrar Tube5 completamente antes de copiar eboot/etiqueta. El servidor conserva
el vídeo de 28:21. Primero basta probar la apertura y 30–60 s: no tiene sentido
una prueba larga si vuelve a fallar al inicio. Si aparece error, fotografiar
error/MEMFAIL/READ y no insistir. No cambiar servidor durante la reproducción.

## Build con puntos de control

```bash
PS5_CLANG=clang USE_CCACHE=0 make APP_DEFINITIONS=TUBE5_DIAGNOSTICS BUILD_LABEL=BOOT-DIAG-22-HTTP \
  APP_WRAP_SYMBOLS="PS5_Keyboard_Init PS5_Keyboard_Open sceVideoOutOpen SDL_CreateRGBSurface_REAL avcodec_open2 SDL_CreateThread_REAL pthread_create SDL_RunThread SDL_SYS_SetupThread SDL_LockMutex_REAL av_read_frame avcodec_send_packet avcodec_receive_frame avformat_seek_file malloc calloc realloc posix_memalign Kit_LockVideoDecoderRaw Kit_BeginPacketBufferRead Kit_FinishPacketBufferRead Kit_CancelPacketBufferRead Kit_GetTimerElapsed av_frame_ref"
```

Cerrar Tube5 y sustituir la carpeta completa `dist/PPSA99999/` en la consola,
incluyendo `sce_module/` y el nuevo `sce_sys/param.json`. No cambiar los payloads
ni el firmware para esta prueba.
Grabar los avisos y comunicar el último código Dxx visible. Para DIAG13-HTTP,
añadir también `network-test-url.txt` y seguir la guía HTTP enlazada arriba.
Si ya está instalada la build 8, 9, 10 u 11 completa, para esta actualización sólo cambian
`eboot.bin` y `build-label.txt`; basta con sustituir esos dos archivos con Tube5
cerrado. Abrir Vídeo de prueba con una pulsación de X y esperar unos segundos.
Para la build 12, grabar primero el clip completo sin pulsar nada. Si llega a
20 segundos sin crash, volver a abrirlo y probar pausa/reanudación y un salto de
5 s. Comparar `MS LOOP/WORK`, `SYNC DROP`, `FR/RAW`, `MEMFAIL` y `YUV SCALE` con
DIAG11 cerca de los mismos segundos del clip, junto con las tres
líneas del pipeline. El aviso de primer frame ya no aparece desde la build 10.

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
| VIDEO: primer frame subido (builds 6–9) | Copia RGBA a textura correcta; no prueba imagen en movimiento; eliminado en build 10 |

Los avisos se pueden encolar: el último visible es una pista, no una traza de
crash. Si SDL devuelve un error, se muestran etapa y detalle y la app permanece
viva para leerlos. El hook anterior al runtime no usa heap, stdio ni SDL.

Para una build normal, omitir `APP_DEFINITIONS` y `BUILD_LABEL`: los avisos de
diagnóstico quedan desactivados. La compatibilidad de esta build sigue pendiente
de la prueba en la consola.
