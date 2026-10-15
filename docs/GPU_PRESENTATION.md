# Presentación eficiente a 1080p

Prioridad acordada: validar eficiencia y presentación GPU a 1080p antes de
habilitar resoluciones mayores. Conservar DIAG65 como baseline funcional y
la ruta CPU/SDL como fallback. No afirmar ahorro de CPU/energía sin medirlo.

## Ruta elegida para el primer experimento

DIAG66 es un experimento aislado que usa el SDK público ps5-opengl 1.0.1
(GPL-3.0-or-later) para presentar un frame NV12 propio con shaders escritos
para Tube5. El SDK se descarga a `.deps/gpu-sdk/` y no se versiona. Su paquete
publicado solo tiene comprobaciones de host; la prueba en FW13.20 sigue pendiente.
ProsperoLight y PS5_Vulkan fueron referencias de investigación, pero sus
recursos privados no entran en la build.

La ruta actual copia NV12 nativo a un AVFrame, convierte a IYUV y compone con
SDL/CPU. El objetivo es que un shader muestree los planos NV12, aplique matriz
y rango correctos y escriba el target que escanea VideoOut. Audio, reloj,
extracción, SponsorBlock y lógica de seek permanecen en el player existente.

## Pasos acotados

1. DIAG64: cargar módulos del sistema AGC/driver, resolver exports, llamar
   `sceAgcInit(8)` y consultar la existencia de defaults sin dereferenciarlos.
   No hay shaders, comandos, submissions ni modificación de VideoOut.
   DIAG64 no pudo cargar los módulos por dlopen en FW13.20. DIAG65 usa la
   ruta de imports enlazados del título para init/defaults y un query driver,
   sin distribuir módulos del sistema. DIAG65 validada en FW13.20: init 0,
   defaults presentes, query driver 32 dwords, cero submissions. Todavía no
   demuestra ninguna imagen renderizada ni ahorro de CPU.
2. DIAG66: presentar un frame NV12 propio por GPU y comprobar color y crop
   1088→1080 contra cinco muestras CPU. Los dos planos se suben a texturas;
   esto valida la ruta gráfica, pero todavía no elimina copias ni valida vídeo.
3. Clip corto 1080p: sincronizar consumo GPU con el decoder y el reloj, luego
   introducir controles y overlay. Un único propietario de los flips VideoOut.
4. Comparar tiempos de copias/conversión/composición y ocupación del hilo CPU
   con la baseline, incluyendo pausa/seek y cierre. Reducir las copias restantes
   solo después de validar ownership. Resoluciones mayores quedan para después.

## Reglas de propiedad

- Nunca reutilizar un slot NV12 que aún pueda leer la GPU. Mantenerlo hasta el
  fence/completion correspondiente, no solo hasta acabar el submit en CPU.
  DIAG66 usa `glFinish` antes de liberar el frame.
- En timeout, no liberar memoria que la GPU pueda seguir usando; registrar el
  fallo y evitar reusar recursos inciertos. Preservar fallback/recuperación.
- Tablas y descriptores dereferenciados por GPU deben estar en memoria accesible
  por GPU; respetar coherencia y barreras, no pasar tablas de stack.
- Evitar competir con los flips CPU del renderer existente y restaurar el menú
  al terminar el player. No subir una build sobre una app abierta.

DIAG66 se compila con `bash tools/build-gpu-frame.sh` y escribe
`/download0/tube5-gpu-frame.log` y `/download0/tube5-gpu-decoder.log`, sin
tokens ni URLs. Sustituye temporalmente la interfaz por una imagen estática y
queda abierta para inspección; se cierra desde el menú PS. La build estable
DIAG65 se conserva en la consola antes de activar DIAG66.

DIAG66 validada por consola en FW13.20: el usuario vio el frame a pantalla
completa; el log reporta `GL ERROR 0` y cinco muestras con cero diferencias.
VideoDec2 entregó 1920×1088, pitch 2048, y terminó con `CLEANUP 0` y
`QUARANTINED 0`. Esto valida un frame y la presentación GPU; no mide todavía
rendimiento ni valida reproducción continua, seek o ownership sin copias.

La prueba continua (iniciada en DIAG67) reproduce el fixture propio
`hw-bclip.t5hc`: 150 frames, 1080p30, 5 segundos y B-frames. Compila los shaders
una vez, reserva dos texturas y actualiza sus planos con `glTexSubImage2D`.
Mantiene cada frame nativo hasta terminar dibujo y swap con `glFinish`, y libera
decoder, texturas, VAO y programa al finalizar. El contexto EGL queda abierto
para conservar la última imagen y permitir leer logs por FTP.

Los informes son `/download0/tube5-gpu-clip.log` y
`/download0/tube5-gpu-clip-decoder.log`. Las muestras de color se comprueban en
el primer y último frame; el decoder valida orden y CRC. Los máximos de
upload/draw/swap miden tiempo transcurrido en esas llamadas, no ocupación CPU;
la espera de pacing queda fuera de esos máximos. DIAG66 y DIAG65 permanecen
respaldadas.

DIAG67 validada en consola: 150 enviados/decodificados/dibujados, 150 CRC
correctos, cero errores de orden, diez muestras RGB sin diferencias y
`GL ERROR 0`. El usuario confirmó que termina en el frame 149. Decoder y
recursos GL se liberaron; `CLEANUP 0`, `QUARANTINED 0`.
El elapsed fue 5.657.421 µs y se contaron 150 frames tarde respecto al reloj
iniciado antes del primer frame. Máximos de llamadas upload/draw/swap:
37.174/16.619/345 µs. Estos máximos y el coste de readback inicial no establecen
todavía rendimiento sostenido de 30 fps; el contador SLOW del decoder incluye
esperas de pacing en el callback y tampoco mide coste CPU puro. El siguiente
paso es medir el tramo continuo tras el primer frame y refinar upload/pacing.

DIAG68 separó arranque y reproducción continua y agrupó dibujo/swap con una
espera final. Los 150 frames y las diez muestras RGB pasaron, pero el tramo
continuo tardó 5.648.655 µs (unos 26,4 fps): intervalos p50/p95/máximo
37.519/41.062/44.898 µs y trabajo gráfico sin pacing/oracle
37.206/37.742/39.466 µs. Hubo 143 retrasos mayores de un frame. El retraso
persistía tras el arranque.

DIAG69 intentó usar el NV12 como un texture buffer `R8`, pero la comprobación
de tamaño devolvió `-19` antes de crear el decoder o presentar un frame. El
`SIGSYS` posterior se produjo al volver de `main()` y llamar a `exit()`;
el backtrace `0x4001b3` coincide con esa llamada del CRT. La recuperación con
DIAG66 permitió leer el informe persistente. Una aplicación distinta abierta
causó además `Device busy` en ShadowMount, resuelto al cerrarla.

DIAG70 (`bash tools/build-gpu-clip.sh`) empaqueta NV12 en texels `RGBA32UI`
de 16 bytes. El shader extrae Y/U/V mediante índices y operaciones enteras;
no se realiza conversión ni swizzle por píxel en CPU. La reserva se divide en
hasta ocho texture buffers si lo exige el límite consultado. En esta consola,
`GL_MAX_TEXTURE_BUFFER_SIZE` es 1.048.576 texels, hay 16 unidades de textura
y basta un bloque de 6.684.672 bytes. El mismo límite en `R8` explicaba el
rechazo de DIAG69. Sigue habiendo copia CPU del NV12 a un buffer GL, por lo que
no es una ruta sin copias. Los errores propios de preparación conservan ahora
el proceso abierto para inspeccionar logs, en lugar de retornar de `main()`.

DIAG70 validada en FW13.20: 150 enviados/decodificados/dibujados, 150 CRC
correctos, diez muestras RGB correctas, cero errores de orden, `GL ERROR 0`,
`CLEANUP 0` y `QUARANTINED 0`. El tramo entre primer y último frame fue
4.966.807 µs (149 intervalos, aproximadamente 30 fps), con cero retrasos
mayores de un frame. Intervalos p50/p95/máximo: 33.335/33.390/33.868 µs.
Trabajo gráfico sin pacing ni oracle p50/p95/máximo: 4.214/6.446/6.571 µs;
medianas de llamadas upload/draw/swap+finish: 403/3.369/468 µs. El arranque
costó 68.204 µs. La reducción frente al trabajo mediano de DIAG68 es del 89 %
en tiempo transcurrido, no una medición de consumo CPU o energía. El contexto
permanece abierto; un ciclo no valida ausencia de crecimiento entre reaperturas.
El siguiente paso es integrar el presenter en el reproductor con un único
propietario de VideoOut, audio, pausa y seek a 1080p.

DIAG71 integra EGL como único presenter del menú y vídeo. SDL conserva el
compositor offscreen, audio, eventos y mando; Kitchensink entrega NV12 en vez
de IYUV, sin crear una textura SDL de vídeo. El presenter conserva dos buffers
GL empaquetados para UI y NV12 y espera la finalización tras cada swap antes de
actualizarlos. Se mantienen las copias nativo→AVFrame y AVFrame→buffer GL.
El adaptador del heap público se encadena con los wrappers/pools existentes;
una copia generada de PSBC cambia siete símbolos Parson que colisionan con
`json-c`, conservando las bibliotecas SDK originales.

La build nativa y los tres shaders pasaron; también las comprobaciones de ABI
de pools RefStruct/parser/SDL. En consola, el menú se mostró correctamente y el
usuario confirmó audio, pausa y seek, pero el vídeo salió verde. Los logs
registraron decoder y cleanup sin errores, tres resets y 248 frames entregados.
DIAG72 separa las unidades de textura UI/vídeo y compara un píxel central CPU
con readback GPU del primer frame visible. Esto distingue datos de entrada
incorrectos de interpretación o estado gráfico incorrecto, sin fotos. Su
comparación en consola devolvió CPU y GPU `0/125/0`, con cero diferencias:
el verde ya estaba en el NV12 que recibía el presenter. El informe es
`/download0/tube5-gpu-player.log`; el cierre conserva el último resultado de
color para poder leerlo al volver al menú.

DIAG73 evita swscale cuando el sink GPU recibe un frame ya NV12 a sus dimensiones
originales: `av_frame_ref` conserva planos, pitch, timestamps y propiedad a
través de la cola de Kitchensink. Un test enfocado mueve esa referencia a la
cola, libera el frame de entrada y verifica que ambos planos siguen válidos.
Los frames de otros formatos mantienen la conversión existente a NV12.
También sustituye el triángulo sobredimensionado por un quad de dos triángulos
dentro del clip, para corregir la esquina superior derecha ausente.

DIAG73 validada con el clip local 1080p: el usuario confirmó color y rectángulo
correctos; CPU y GPU `0/15/255`, cero diferencias. VideoDec2 entregó 150 frames,
con EOF y cleanup sin error; el NV12 nativo central reportó `41/240/111`.
La cola nueva preserva el color y evita NV12→NV12 innecesario. La subida máxima
medida al buffer GL fue 484 µs. El máximo global de composición (1,41 s) incluye
el primer render/compilación del backend y no establece coste sostenido.
Al cerrar el player, `ORACLE` se reinicia para la siguiente sesión, mientras se
conservan los últimos RGB comparados y su resultado en el informe.
Audio, pausa y seek fueron confirmados por el usuario durante la integración;
la comprobación siguiente es YouTube directo 1080p con esta misma build, antes
de aumentar resolución. DIAG70 y DIAG65 continúan respaldadas.

DIAG73 también validada con YouTube directo 1080p: el usuario completó la
prueba de reproducción, pausa y saltos y salió del player. VideoDec2 entregó
3.453 frames y cerró con `ERROR 0`, `NATIVE ERROR 0`, `CLEANUP 0`; siete resets
quedaron registrados. El presenter reportó `ERROR NONE`, cero diferencias de
color (CPU/GPU `32/41/60`) y un máximo de subida NV12 de 505 µs. El último
informe de audio tras seek registró `HOLD 0`, resets de vídeo/audio 6/6,
196.608 bytes PCM adicionales y `ERROR 0`. Hay 133 callbacks contabilizados
como underrun en 5.416 callbacks; el contador incluye periodos de espera de
seek y no establece ausencia de cortes audibles. El máximo global de
composición sigue incluyendo el arranque, por lo que no se usa como rendimiento
continuo. La ruta 1080p integrada queda como nueva baseline; resoluciones
superiores requieren revisar formatos YouTube y límites del decoder, además
de dimensiones y propiedad de los buffers.

## VP9 1440p: primer frame, DIAG76

En FW 13.20 se validó un keyframe propio VP9 Profile 0 de 2560×1440,
generado mediante `tools/prepare-vp9-frame.sh`. El decoder devolvió NV12 de
2560×1440 y pitch 2560; el CRC de muestras Y/U/V fue `19DECBFB`, idéntico al
decode software de referencia. El presenter GPU mostró la imagen y su píxel
central CPU/GPU coincidió (`0/0/252`). Subida del frame: 509 µs; una sola
composición: 27.925 µs, no una medida de rendimiento continuo.

DIAG74/75 se detenían en nuestro límite de memoria compartida de 64 MiB:
la consulta nativa requiere `0x064D2400` bytes, aproximadamente 101 MiB.
DIAG76 permite hasta 128 MiB en ese bloque, manteniendo el presupuesto total
de 256 MiB, la validación de tamaños/alineación y la propiedad de buffers.
Create, decode y cleanup devolvieron cero. La memoria flexible final fue
1 MiB inferior a la inicial; una ejecución no demuestra ausencia de fugas
y el heap gráfico permanece abierto para mostrar la imagen.

La prueba aislada está en `experiments/gpu-vp9/`, se compila con
`tools/build-vp9-probe.sh` y escribe `tube5-vp9-1440.log` y
`tube5-vp9-1440-decoder.log` en `/download0/`. El frame 1440p se presenta
escalado a una salida 1920×1080: todavía no valida reproducción continua,
YouTube VP9, audio, seek, salida HDMI 1440p ni 4K. La baseline funcional
DIAG73 queda respaldada en consola.

Referencia pública de configuración VP9: ProsperoTV, commit
`f3142ce3ccb4a1d125b08a58fde1b5723325e4ea`; investigación de VideoDec2,
commit `c6439d9f7a3c1bb6dcc4f66f3e7e0f728d3e9dd1`. El siguiente paso es
validar una secuencia VP9, incluidos los cuadros ocultos y superframes,
antes de habilitar los formatos superiores en el selector YouTube.

## VP9 1440p: secuencia visible de 3 segundos, DIAG77

`tools/prepare-vp9-clip.py` genera únicamente la nueva secuencia necesaria:
90 frames VP9 Profile 0 a 30 fps, sin lag, alt-ref, cuadros ocultos ni
show-existing. Su contenedor T5HC versión 3 incluye tamaños, índices de
presentación y CRC de muestras de luminancia obtenidos del decode software.
El parser valida la estructura completa y los límites antes de crear el
decoder. Se reutiliza la ruta síncrona de profundidad 1; para VP9 esta prueba
exige una imagen por entrada y no hace flush entre paquetes.

En consola FW 13.20, DIAG77 entregó y presentó los 90 frames, con 90 CRC
correctos y 89 cambios de luminancia. `RESULT 0`, `CLEANUP 0`, sin cuarentena,
sin falta de salida ni errores de orden. Entre el primer y último flip
transcurrieron 2.978.435 µs para 89 intervalos (aproximadamente 29,9 fps).
Intervalos p50/p95/máximo: 33.320/35.312/47.180 µs. Trabajo de presentación
p50/p95/máximo, excluyendo el primer frame: 1.918/1.962/2.027 µs.
El primer frame incluye inicialización/compilación; el contador del decoder
registró un frame lento. `LATE 0` mide retraso al iniciar una iteración,
no ausencia de variación entre flips. El informe del presenter captura
solo el primer frame en esta secuencia; no es un resumen de los 90 uploads.

La diferencia final de memoria flexible fue nuevamente 1 MiB con el contexto
GPU aún abierto. Esta prueba corta no establece estabilidad prolongada o
ausencia de fugas. Los informes específicos son `tube5-vp9-clip.log` y
`tube5-vp9-clip-decoder.log` en `/download0/`. La salida sigue siendo 1080p;
pendientes VP9 con cuadros ocultos/superframes, integración YouTube/audio,
seek y 4K. DIAG76 queda respaldada junto a la baseline funcional DIAG73.

## VP9 1440p: cuadros ocultos y superframes, DIAG79

El generador `tools/prepare-vp9-clip.py --altref` crea solo la nueva fixture
T5HC versión 4: 90 imágenes visibles, 6 cuadros ocultos y 6 superframes.
Para activar alt-ref en libvpx se usa codificación de dos pasadas, con sus
estadísticas en un directorio temporal. No contiene show-existing-frame.
`src/vp9_packet.hpp` valida los índices, tamaños y Profile 0 antes de exponer
las partes; su test enfocado comprueba casos malformados y los 90 paquetes
reales de la fixture. El decode software proporciona un CRC de luminancia
por imagen visible. El hardware recibe las partes del superframe por separado,
en orden; los cuadros ocultos no se suben a GPU ni avanzan el reloj visible.

DIAG78 se detuvo tras la primera imagen al exigir salida válida para el
primer cuadro oculto. DIAG79 acepta una salida ausente de un cuadro oculto
solo cuando el driver no retiene la superficie ofrecida (`accepted == 0`),
sin flush intermedio ni reutilización de una superficie retenida.
El cierre conserva ahora la etapa del error de decode en lugar de sustituirla
por `DISPLAY`.

En FW 13.20, DIAG79 procesó 96 cuadros codificados, mostró 90 imágenes y
confirmó los 90 CRC; 6 ocultos y 6 superframes, `RESULT 0`, `CLEANUP 0`,
sin cuarentena. Un oculto no produjo salida y no retuvo la superficie;
los otros cinco produjeron imágenes válidas descartadas. Entre 89 intervalos
transcurrieron 2.969.425 µs, aproximadamente 30 fps de media. Intervalos
p50/p95/máximo: 33.263/45.157/53.647 µs; no demuestra cadencia perfectamente
uniforme. Trabajo de presentación p50/p95/máximo, sin primer frame:
1.914/1.969/2.058 µs. La memoria flexible final volvió a quedar 1 MiB por
debajo de la inicial con el contexto GPU abierto.

Informes: `tube5-vp9-altref.log` y `tube5-vp9-altref-decoder.log` en
`/download0/`. La salida continúa escalada a 1080p; siguen pendientes
show-existing-frame en consola, YouTube/audio/seek y estabilidad prolongada.
Estos resultados no habilitan aún todos los formatos VP9 en el player real.

## VP9 1440p: reproductor integrado y seek, DIAG83

`tools/prepare-vp9-player.py` remuxa los 90 paquetes de la fixture DIAG79
cuatro veces, con timestamps continuos y AAC: control local de 12 segundos,
sin recodificar vídeo. Se comparan los hashes de los 360 paquetes comprimidos
contra cuatro repeticiones de la fuente. En la build de control, `LOCAL`
abre `hw-vp9-player.mp4`; HTTP sigue siendo el valor predeterminado del menú.
El selector YouTube todavía no habilita 1440p.

El adaptador nativo conserva la ruta H.264 1080p y añade VP9 Profile 0
2560×1440. `vp9_superframe_split` entrega las partes; se decodifican con
profundidad 1 y los ocultos se descartan antes de la cola del reproductor.
Los timestamps VP9 se asocian a la entrada síncrona, sin consultar metadatos
H.264 ni permitir salida demorada/superficies retenidas sin imagen. Los
frames NV12 propios conservan el pitch y sus dimensiones originales hasta
la subida GPU; la presentación sigue siendo 1080p.

DIAG80/81 entregaron 15 frames antes de rechazar el segundo oculto por el
byte 11 de `VideoOutput`, denominado `padding` en nuestro layout, que el
driver establece a 1 en esa salida válida. DIAG82 deja de exigir cero para
ese byte en VP9, sin relajar formato, dimensiones, capacidad ni propiedad
del buffer. El test enfocado cubre ocultos con y sin salida, ese byte,
timestamps, reset, EOF, cierre y rechazo de una superficie inválida.

El propietario confirmó reproducción con DIAG82, pero el seek mostraba
vídeo previo durante 1–2 segundos. DIAG83 conserva la última imagen mientras
se completa el reset y descarta VP9 anterior al destino antes de entregarlo
a Kitchensink. `native_seek_gate.hpp` separa esta puerta del filtro existente
`seek_preroll.hpp`; su test cubre generaciones antiguas, saltos hacia delante
y atrás, sustitución del destino y cancelación. El audio combinado se silencia
durante la espera, sin impedir el drenaje de PCM/actualización del reloj.

DIAG83 en consola: el usuario confirmó mejor seek. La última sesión registró
1.486 entradas, 1.387 imágenes recibidas, 1.147 entregadas y 240 descartadas
como preroll; 17 resets, 99 ocultos y EOF. Errores del adaptador/decoder y
cleanup: cero. El informe de audio posterior al último seek muestra target
3.468 ms, `HOLD 0`, resets vídeo/audio 15/15 y 122.880 bytes PCM adicionales,
con `ERROR 0`. Sus 316 underruns en 1.609 callbacks incluyen silencios de
seek y no demuestran ausencia de cortes audibles. El máximo de subida GPU
del proceso fue 708 µs; el máximo de composición incluye el arranque.
La comparación de color integrada figura como `ERRORS -1` (no medida para
esta sesión/resolución), no como un nuevo CRC de los frames integrados.

Pendiente YouTube VP9 1440p con AAC separado y sesiones largas. Esta prueba
no valida aún show-existing-frame, 4K, 60 fps ni salida HDMI 1440p.

## YouTube directo VP9 1440p, DIAG84

El selector permite únicamente VP9 Profile 0, 8 bits, 2560×1440 y hasta
30 fps junto a AAC separado. Auto sigue limitado a 360p. La consulta valida
la cabecera MP4/WebM según el formato seleccionado y el transporte elige
`mov` o `matroska` conservando HTTPS, certificados, límites y rangos.
El test enfocado cubre selección VP9/AAC y rechazo de Profile 2, 10 bits,
60 fps, 4K, dimensiones incompatibles y contenedores truncados; también
pasó el test offline de transporte y la build nativa.

El propietario confirmó reproducción, audio y controles en YouTube 1440p,
con algunos saltos algo lentos. Los logs rescatados registraron 5.826
entradas, 5.385 imágenes recibidas, 3.313 entregadas, 2.072 descartadas como
preroll, 441 ocultos y 33 resets. Cierre, adaptador y decoder: cero errores.
No hubo EOF en la última sesión, así que no se afirma reproducción hasta el
final. Audio después del último seek: `HOLD 0`, resets 31/31, 192.512 bytes
PCM adicionales y error cero. Los 1.109 underruns en 6.699 callbacks incluyen
esperas de seek; no cuantifican cortes audibles. Subida GPU máxima del proceso:
698 µs; la comparación de color integrada queda no medida (`ERRORS -1`).
Los logs no proporcionan latencia individual de seek ni cadencia sostenida
por frame. Pendientes sesiones largas, salida HDMI nativa 1440p y 4K/60 fps.
DIAG84 queda como baseline de YouTube 1440p; los logs privados se conservan
en `.local/console-logs/`, fuera de Git.

## VP9 4K/30: primera secuencia, DIAG85

`tools/prepare-vp9-clip.py --4k` genera únicamente la nueva fixture 3840×2160:
90 frames visibles a 30 fps, sin cuadros ocultos/superframes. T5HC versión 5
se valida antes del decoder; el test enfocado con sanitizers comprueba el
layout, CRC, dimensiones y cleanup. El presenter admite buffers NV12 hasta
3840×2160, dentro del límite de texels RGBA32UI del SDK. Para la configuración
VP9 4K exclusivamente se permiten bloques GPU/compartido de hasta 256 MiB,
con total acotado a 512 MiB; H.264 y VP9 1440p conservan sus límites anteriores.
El log conserva los requisitos consultados para diagnosticar un rechazo sin
otra build de marcadores.

En FW 13.20, el propietario confirmó visualización correcta. Los 90 frames
se enviaron, decodificaron y presentaron con 90 CRC correctos, 89 cambios y
`RESULT 0`, `CLEANUP 0`, sin cuarentena. Tiempo entre 89 flips: 2.985.306 µs,
aproximadamente 29,8 fps. Intervalos p50/p95/máximo: 32.747/37.671/54.903 µs;
dos inicios tardíos y tres frames contabilizados como lentos (incluye inicio).
Trabajo de presentación sin primer frame p50/p95/máximo: 2.642/2.801/2.839 µs.
Decode máximo: 33.994 µs. No demuestra cadencia perfectamente uniforme ni
estabilidad prolongada.

Requisitos del decoder: CPU 12.588.320, GPU 171.327.744, compartido
178.574.080 y superficie 12.541.952 bytes. La diferencia final de memoria
flexible volvió a ser 1 MiB con el contexto GPU abierto. Logs privados
rescatados en `.local/console-logs/diag85-*`, fuera de Git.

La fuente sí es 4K, pero la salida permanece en 1920×1080. Pendientes
integración 4K en el player real, audio/seek, YouTube, cuadros ocultos,
salida HDMI 4K y 60 fps. DIAG84 funcional queda respaldada en consola.

## VP9 4K: reproductor local con AAC y controles, DIAG86

La ruta nativa síncrona añade el modo VP9 Profile 0 3840×2160, nivel máximo
5.1, manteniendo H.264 1080p y VP9 1440p. El reset preserva las dimensiones
configuradas. El presenter ya acepta 4K; el player mantiene frames NV12 a
resolución de origen con presentación 1080p. El test enfocado del stream 4K
cubre propiedad, ocultos simulados, timestamps, resets, EOF y cleanup, además
del rechazo de buffers inválidos, con ASan/UBSan.

`tools/prepare-vp9-player.py --4k` remuxa cuatro repeticiones de los 90 paquetes
de DIAG85, sin recodificación, con AAC y timestamps continuos: 12 segundos.
Los hashes de los 360 paquetes se comparan con la fuente. En DIAG86, `LOCAL`
abre `hw-vp9-4k-player.mp4`; YouTube 4K todavía no está habilitado.

El propietario confirmó reproducción, pausa, saltos y reapertura. Los logs
de la última sesión muestran 1.028 imágenes recibidas, 866 entregadas,
162 descartadas como preroll, 19 resets y EOF. Decoder, adaptador y cierre:
cero errores. Salida nativa NV12 3840×2160, pitch 3840, superficie
12.550.144 bytes dentro de su capacidad. Audio después del último salto:
`HOLD 0`, resets 17/17, 180.224 bytes PCM adicionales y error cero.
Los 370 underruns en 1.080 callbacks incluyen espera de seek y no establecen
ausencia de cortes audibles. Subida GPU máxima del proceso: 1.546 µs; el
máximo de composición incluye arranque. La comparación de color integrada
sigue no medida (`ERRORS -1`), no se equipara al CRC de DIAG85.

La fixture no contiene ocultos ni superframes reales de 4K: su comportamiento
nativo continúa pendiente, junto a YouTube 4K/AAC separado, sesiones largas,
salida HDMI 4K y 60 fps. Los logs privados se conservan en
`.local/console-logs/diag86-*`. DIAG84 funcional permanece respaldada.

## YouTube directo VP9 4K/30 con AAC separado, DIAG87

El selector habilita 2160p únicamente a 3840×2160, VP9 Profile 0 de 8 bits,
hasta 30 fps y nivel 5.1 cuando el codec lo declara. Los descriptores PQ/HLG
extendidos, Profile 2, 10 bits, AV1, 60 fps y dimensiones incompatibles quedan
excluidos; Auto continúa en 360p. El test enfocado del selector comprueba
estas restricciones y la pareja con AAC. No cambia el transporte validado
MP4/WebM ni la ruta de decode probada en DIAG86.

El propietario confirmó funcionamiento general en YouTube 4K y mayor latencia
ocasional de seek/timeline que en calidades menores. La sesión registrada
cerró con 7.945 entradas, 7.361 imágenes recibidas, 6.190 entregadas, 1.171
descartadas como preroll, 584 ocultos y 21 resets. Errores de adaptador,
decoder y cleanup: cero. El último informe post-seek disponible muestra
resets vídeo/audio 19/19, `HOLD 0`, 192.512 bytes PCM adicionales y error cero;
es anterior a los dos últimos resets del informe de vídeo. Los 872 underruns
en 5.638 callbacks incluyen esperas de seek y no miden cortes audibles.
Subida GPU máxima del proceso: 1.456 µs; la composición máxima incluye inicio.

No hubo EOF en la sesión registrada; no se afirma reproducción hasta el final.
Los logs no separan la latencia por seek en red, decode de referencias/preroll,
colas y presentación. La mayor carga 4K es una explicación posible, no un
diagnóstico confirmado del retraso. Logs privados rescatados en
`.local/console-logs/diag87-*`. Pendientes sesiones largas, medición del seek,
show-existing-frame, salida HDMI 4K, 60 fps y HDR.

## VP9 4K/60: integridad correcta, cadencia insuficiente, DIAG88

`tools/prepare-vp9-clip.py --4k --fps 60` genera únicamente la nueva secuencia
de 180 imágenes visibles, 3 segundos de contenido, sin ocultos/superframes.
T5HC versión 6 exige 60 fps y permite hasta 32 MiB por fixture; versiones
anteriores mantienen 30 fps y 16 MiB. El test enfocado con sanitizers valida
el nuevo esquema, CRC/cleanup y rechazo de una cabecera v6 con fps incorrecto.

En FW 13.20, el propietario vio el clip. Los 180 frames se decodificaron y
presentaron con 180 CRC correctos, 179 cambios, errores y cleanup cero.
Los 179 intervalos entre flips tardaron 3.587.037 µs: aproximadamente
49,9 fps reales, NO 60 fps sostenidos. Intervalos p50/p95/máximo:
18.803/23.132/44.587 µs. Trabajo de presentación sin primer frame
p50/p95/máximo: 2.647/2.824/3.701 µs. Decode máximo 34.787 µs; el log no
contiene percentiles de decode, lectura o checksum. `SLOW` no es una medida
independiente: suma el tiempo del consumer y el de presentación que ese mismo
consumer ya incluye. La cadencia real entre flips es el criterio de esta prueba.

La prueba es secuencial (lectura, decode, CRC, subida, presentación); el
reproductor real usa un worker de decode separado del render. No se atribuye
todavía el déficit a un límite del decoder hardware. Hay que medir las fases
antes de decidir optimizaciones o dar por validados 60 fps. Los requisitos de
memoria coinciden con DIAG85. Logs privados: `.local/console-logs/diag88-*`.

Candidato de YouTube verificado solo por metadatos, sin descargar vídeo:
`LXb3EKWsInQ` (Costa Rica, duración 314 s). El 2026-10-12 ofrecía 3840×2160
a 60 fps SDR VP9: itag 315 WebM `vp9` e itag 628 MP4 `vp09.00.51.08`.
También HDR10 Profile 2 de 10 bits: itag 337 WebM e itag 642 MP4.
La disponibilidad en nuestro cliente de consola y la reproducción siguen
pendientes. El selector Tube5 aún excluye 60 fps y HDR.

## VP9 4K/60: costes por fase, DIAG89

La misma fixture DIAG88 se reutiliza sin regenerarla. `TUBE5_HW_TIMING` añade
medición de lectura, decode, CRC, consumer, paso completo, log y espera del
reloj solamente al probe. Se excluye el primer frame de los percentiles.
La prueba enfocada verifica que las fases registran tiempos y que su suma
cabe en el paso total. `hardware_clip_presented(0)` elimina la doble cuenta
del tiempo de presentación, ya incluido en el consumer. La iteración de EOF
no escribe fuera del array de 180 muestras.

DIAG89 en FW 13.20: 180/180 frames y CRC correctos; resultado y cleanup cero.
Los 179 intervalos tardaron 3.580.057 µs (aproximadamente 50 fps). Medias:
lectura 70 µs, decode 16.860 µs, CRC 86 µs, consumer 2.687 µs, paso completo
19.918 µs y log 208 µs. Decode p50/p95/máximo: 16.018/20.053/34.148 µs;
consumer: 2.650/2.790/4.850 µs. Los logs son esporádicos y llegan a 4.008 µs,
pero no son el coste dominante. La espera media del reloj fue 93 µs.

La prueba secuencial supera el presupuesto de 16.667 µs para 60 fps: el decode
ya está alrededor del presupuesto y sumar presentación lleva a ~20 ms.
Quitar el CRC no resolvería ese déficit. El siguiente ensayo es el player
real con decode/render en hilos distintos, midiendo la cadencia, descartes y
sincronización; el solapamiento puede ayudar, pero no garantiza 60 fps.
No se da 4K/60 por validado ni se atribuye aún un límite absoluto al hardware.
Logs privados: `.local/console-logs/diag89-*`.

## VP9 4K/60: player integrado queda atrasado, DIAG90

El control remuxa la misma fixture 4K/60 cuatro veces (12 segundos), con AAC,
sin recodificar vídeo. Se verifican los hashes de sus 720 paquetes. El remux
de ~106 MiB se genera con `tools/prepare-vp9-player.py --4k --fps 60` y queda
fuera de Git; el generador y la fixture comprimida bastan para reproducirlo.
`LOCAL` abre este control; YouTube 60 fps sigue deshabilitado.

Se añaden tiempos de decode/copia y un informe de cadencia de uploads al cerrar
el player, después de unir los workers. El test enfocado verifica conteo,
discontinuidades de pausa/seek, timestamps y conversión del reloj sin multiplicar
directamente un contador absoluto grande. La cadencia de uploads no equivale
a medir flips HDMI; pausa y seek rompen los intervalos para no falsear su media.

El propietario reportó imagen detenida con reloj avanzando; seek recupera
brevemente la imagen y vuelve a detenerse. DIAG90 registró 1.527 imágenes
recibidas, 1.495 entregadas por el adaptador, 32 preroll, cinco resets y EOF,
con errores y cleanup cero. Solo se subieron 132 imágenes y Kitchensink
descartó 1.359 por sincronización. Decode medio 16.977 µs, máximo 34.803 µs;
copiar a AVFrame añade 1.611 µs de media, máximo 2.023 µs.
La producción de frames queda por debajo del reloj de 60 fps y el descarte
por retraso deja la última imagen visible. No es un decoder parado ni ENOMEM.

El informe registra tres seeks, seis discontinuidades, 128 intervalos de upload,
2.286.900 µs activos y 55,971 fps en esos tramos visibles. Esa cifra excluye las
congelaciones cortadas por seek y NO establece reproducción sostenida a 56 fps.
Audio: 957 underruns en 1.369 callbacks, error cero; este ensayo no valida
estabilidad de audio/vídeo. Logs privados: `.local/console-logs/diag90-*`.

Header tracing del control confirmó `tile_cols_log2=3` (ocho columnas) y
`tile_rows_log2=0`. La referencia pública documenta mejoras con cuatro columnas
frente a una, no frente a ocho; comparar nuestro control con cuatro columnas
es una hipótesis de prueba, no una optimización garantizada. No relajar las
tolerancias de sincronización para ocultar el retraso. 4K/60 sigue pendiente.

## VP9 4K/60: cuatro columnas no resuelven el control, DIAG91

Se cambió únicamente `tile-columns` del encoder a 2 (cuatro columnas);
fuente testsrc2, dimensiones, fps, duración, encoder/preset/CRF, GOP, audio,
pipeline y sincronización se mantuvieron. Header tracing verificó las 180
cabeceras. Se generaron CRC software propios; el remux de 12 segundos mantiene
los hashes de cuatro repeticiones de la nueva fuente. La fixture pesa
27.623.175 bytes frente a 27.674.629 antes (~0,19% menos). El remux grande
queda fuera de Git, generado con `--tile-columns 2` en ambos preparadores.

El propietario confirmó que la imagen vuelve a pararse. Sin pausa ni seek,
el decoder entregó las 720 imágenes, con EOF y cleanup cero. Decode medio
16.838 µs, máximo 36.129 µs; copia media 1.580 µs, máximo 1.969 µs.
La diferencia respecto a DIAG90 es pequeña y sus condiciones de interacción
no fueron idénticas; no se atribuye una mejora significativa al cambio.
Solo se subieron 46 imágenes, 673 se descartaron por sincronización. Sus
45 intervalos abarcan 788.812 µs; el dato de 57,048 fps mide ese breve tramo
inicial, no los 12 segundos ni una reproducción sostenida.
Audio: 505 underruns en 630 callbacks, error cero. La prueba falla el objetivo
de reproducción 4K/60 fluida pese a la integridad y cierre correctos.

Logs privados: `.local/console-logs/diag91-*`. El siguiente control propuesto
es mayor profundidad de pipeline en un probe aislado, con buffers de entrada
y salida separados, retención/correlación de imágenes demoradas, CRC y drain
al final. No cambiar profundidad en el player síncrono actual sin validar
esas garantías, ni relajar sincronización para encubrir el retraso.

### DIAG92: control aislado VP9 4K60, profundidad tres (pendiente de consola)

`BOOT-DIAG-92-VP9-DEPTH3` reutiliza las 180 imágenes de la fixture de cuatro
columnas de DIAG91, con profundidad tres y seis slots independientes. No cambia
el player de producción ni habilita YouTube 60 fps. Cada input conserva su slot
hasta la salida FIFO correspondiente; cada superficie aceptada queda retenida
hasta que el consumidor termina. Las llamadas sin imagen continúan la carga de
la cola, y EOF utiliza un drain acotado. El CRC software de cada imagen valida
contenido y asociación; no se usa GetPictureInfo para VP9.

Prueba host dedicada `--vp9-pipeline`, ASan/UBSan: priming de tres entradas,
salidas demoradas, tres llamadas de drain, no reutilización de input/superficie,
variante sin superficies aceptadas, rechazo de corrupción/desorden y cleanup.
Build nativa y firma verificadas. Ejecutable y fixture preparados por FTP;
activación pendiente de cierre de la app. Logs nuevos:
`tube5-vp9-depth3.log` y `tube5-vp9-depth3-decoder.log`.
El tiempo de una llamada de envío no equivale a latencia de una imagen con
pipeline; la prueba registra cadencia de presentación, priming, drain e integridad.
Resultado DIAG92: pantalla negra, ninguna entrada enviada. Query de profundidad
tres: CPU 12.992.992, GPU 198.852.864, shared 255.169.792 y frame 12.541.952
bytes. Seis superficies más inputs/compute superan el límite total de 512 MiB;
el guard devolvió `-EINVAL` antes de crear el decoder. La etiqueta de fallo
`DECODER ALIGNMENT` es la última consulta registrada, no prueba de alineación
incorrecta. Cleanup cero, memoria flexible antes/después idéntica. Logs privados
en `.local/console-logs/diag92-*`. No hay medición de rendimiento.

DIAG93 (`BOOT-DIAG-93-VP9-4SLOTS`) reduce solo el pool del probe VP9 a cuatro
slots (profundidad más uno), mantiene el presupuesto y consulta sus necesidades
reales antes de asignar. Los límites de búsqueda de buffers usan el tamaño real
del pool; H.264 conserva seis slots y el stream VP9 de producción no cambia.
La prueba host de retención, FIFO/CRC, priming/drain, rechazo de corrupción y
cleanup pasa con cuatro slots, ASan/UBSan. Compilación/firma verificadas;
pendiente de activación y prueba en consola. Reutiliza los logs `depth3` con
etiqueta de build nueva.

Resultado DIAG93 en consola: el propietario confirmó reproducción del clip.
180 entradas, 180 imágenes compuestas, 180 CRC correctos, 179 imágenes cambiadas;
error, cleanup y errores de orden cero. Priming sin output en dos llamadas y
dos llamadas de drain completan el clip. 179 intervalos de presentación en
2.987.279 µs (~59,92 fps), p50/p95/máximo 16.696/20.192/24.227 µs.
La prueba acotada alcanza la cadencia media objetivo, con jitter; no demuestra
todavía reproducción sostenida con audio/YouTube, hidden frames o seek.

Envío/recepción al decoder medio 1.153 µs (p95 4.246), consumidor medio 2.880 µs,
step medio 4.413 µs y espera de pacing media 12.287 µs, excluyendo primera imagen.
El tiempo de llamada corto refleja trabajo en vuelo, no latencia real de decode.
La primera llamada máxima fue 28.861 µs; no inferir capacidad máxima de fps de
los promedios de llamadas. Memoria flexible antes 908.066.816, después
907.018.240 (delta 1 MiB); cleanup nativo correcto, diferencia sin atribución
confirmada y sin prueba de ciclos todavía. Logs privados `diag93-*`.

Siguiente paso: portar la gestión asíncrona al stream del player con FIFO de
metadatos, flags de hidden/existing, buffers retenidos, drenaje y reset de seek.
No basta cambiar la profundidad del stream síncrono; mantener presupuesto y
validar los casos de no-output y audio antes de habilitar formatos YouTube 60 fps.

### DIAG94: primera integración en el player (pendiente de consola)

`BOOT-DIAG-94-VP9-PIPELINE-PLAYER` aplica profundidad tres/cuatro slots solo al
path local exacto `hw-vp9-4k60-player-tc2.mp4`, control de 12 segundos con AAC.
Mantiene las rutas anteriores de YouTube/VP9 a profundidad uno. La selección de
formatos YouTube no cambia. FIFO por tag de entrada preserva PTS/DTS del frame
demorado y retiene AU/superficies; cada release sigue después de copiar el NV12.
Reset vacía el FIFO tras reset nativo, conserva configuración/profundidad y
permite empezar una nueva generación de seek. EOF hace drain acotado a doce
intentos por generación, no un flush por llamada sin output.

El control no contiene hidden frames. En modo asíncrono se rechazan expresamente
los hidden hasta validar si todos producen salida y cómo correlacionar los que
no la producen; no extrapolar la prueba visible a VP9 real. La ruta síncrona
anterior conserva su manejo probado de hidden/existing. Las instantáneas
añaden profundidad, slots, pico pendiente y llamadas de drain.

Host `--vp9-async-stream`, ASan/UBSan: FIFO/PTS/DTS, input retenido, superficies
retenidas o no aceptadas, backpressure durante préstamo, EOF, reinicios con
entradas en vuelo y guard de hidden. Comprobación focal de `--vp9-stream-4k`
valida que el manejo síncrono previo permanece intacto. Build y firma correctas;
staging FTP realizado, activación pendiente de cierre. En consola probar LOCAL:
primero reproducción continua; después pausa/seek y reentrada, conservar logs.

Resultado DIAG94: el propietario informó congelación. Logs prueban que el control
**no activó** el pipeline: profundidad uno, un slot, pico pendiente uno y cero
drain. Decode medio 16.873 µs/copia 1.547 µs; 720 imágenes entregadas, solo 41
uploads y 679 drops. EOF/cleanup cero. Este resultado no invalida profundidad
tres: la selección consultaba `opening_path_`, que solo rellena `begin_open`;
LOCAL usa `open`, síncrono, y no establece ese campo. Logs privados `diag94-*`.

DIAG95 (`BOOT-DIAG-95-VP9-LOCAL-PIPELINE`) pasa el path real explícitamente a
`complete_open` desde ambas rutas, evitando depender del campo de apertura
asíncrona. Solo el mismo control local activa profundidad tres/cuatro slots.
Añade un guard después de crear el player: si ese control no obtiene decoder
nativo a profundidad tres/cuatro slots, falla de forma explícita en vez de
realizar otra prueba con configuración incorrecta. No cambia decoder,
sincronización, audio ni selección de formatos YouTube. Verificación focal:
compilación de ambos call sites y guard de configuración en runtime; los tests
del decoder ya pasados no se repiten por esta corrección de ruta.

Resultado DIAG95: el propietario confirma que ahora se reproduce. Logs verifican
profundidad tres/cuatro slots, pico pendiente tres y dos llamadas de drain.
Reproducción continua de 12 segundos: 720 entradas/recibidas/entregadas, EOF,
error nativo y cleanup cero. 714 uploads, 713 intervalos en 11.996.010 µs
(59,436 fps), intervalo máximo 34.669 µs, cuatro drops de sincronización;
PTS válidos, sin discontinuidades ni seek. No atribuir los dos frames restantes
no subidos sin evidencia adicional. Decode-call medio 1.376 µs, copia 1.633 µs;
tiempo de llamada no equivale a latencia real del decoder asíncrono.
Audio: 570 callbacks, 2.306.048 bytes PCM, siete underruns, error cero, frente
a 511 underruns en DIAG94. Este control confirma el beneficio del pipeline en
el player visible-only, no YouTube 4K60 ni hidden. Logs privados `diag95-*`.
Pendiente con la misma build: pausa, seek +/-5/barra y reentrada; después validar
hidden/existing antes de activar la ruta asíncrona para vídeos reales.

DIAG95 controles: diez seeks solicitados/ejecutados, doce resets nativos,
150 imágenes de preroll omitidas, EOF y cleanup cero. 1.290 uploads, 1.268
intervalos activos, 59,458 fps, seis drops, PTS válidos. Audio post-seek:
reset vídeo/audio 11/11, gate liberado y delta 159.744 bytes PCM; error cero.
195 underruns en prueba con maniobras no equivalen al caso continuo (siete).
Logs privados `diag95-controls-*`; no atribuir audio completamente pulido.

### DIAG96: correlación de hidden en pipeline, probe aislado

`BOOT-DIAG-96-VP9-HIDDEN-FIFO` usa una fixture nueva T5HC v7: VP9 Profile 0
4K60, cuatro columnas, 180 imágenes visibles y 12 hidden en 12 superframes,
192 cabeceras verificadas. Two-pass libvpx alt-ref, tres segundos, 1.377.864
bytes. CRC software por imagen visible. No show-existing en este control.
Generador: `python3 tools/prepare-vp9-clip.py --4k --fps 60 --altref --tile-columns 2`.

El probe divide cada superframe en AUs con slots independientes, conserva
tag/índice del paquete y flag displayable de cada AU pendiente y ensaya FIFO
de salida. Las superficies hidden se liberan sin presentar; solo las visibles
se comparan con CRC y se muestran. No supone validada la hipótesis de una
superficie por AU: una salida ausente debe fallar por bounds/CRC/drain, no
activar el player ni perder silenciosamente la asociación de PTS. Profundidad
tres, cuatro slots y límites de memoria previos. El stream del player mantiene
su guard para hidden hasta obtener evidencia de consola.

Prueba host focal `--vp9-hidden-pipeline`, ASan/UBSan: superframes, hidden con
padding legítimo, FIFO/CRC, buffers pendientes, drain, corrupción y salida
oculta ausente. Build/firma correctas. Fixture y ejecutable transferidos; el
propietario confirmó app cerrada para activar, con DIAG95 respaldada.
La prueba arranca sola y conserva logs `tube5-vp9-depth3*.log`, build distinta.

Resultado DIAG96: un frame visible/CRC correcto, seis AUs enviadas, tres llamadas
sin output, fallo `PICTURE CRC`, cleanup cero. Se emparejó la salida `B26F359C`
con el visible 1 (`ADBADC4A`), pero el CRC corresponde exactamente al visible 2
en la fixture. FIFO de todos los AUs suprimió antes una imagen como hidden.
Evidencia coherente con el primer hidden sin superficie, no una corrupción
aleatoria del decoder. No se valida el pipeline de hidden. Logs `diag96-*`.

DIAG97 (`BOOT-DIAG-97-VP9-HIDDEN-CRC`) mantiene la misma fixture y configuración.
Calcula CRC antes de asignar metadata: con hidden pendiente y output sin el
indicador de hidden observado, exige CRC del siguiente visible para retirar
el prefijo de hidden sin salida. No retira superficies aún retenidas: ese caso
falla explícitamente. Hidden materializado exige el indicador observado y se
suprime; cada visible conserva su CRC y orden. Cuenta hidden materializados y
omitidos. Esto es un **oráculo de diagnóstico para la fixture conocida**, no
una solución de streaming: no habilita hidden asíncrono del player/YouTube ni
asume que byte 11 tenga semántica universal sin validación adicional.

Host ASan/UBSan focal: hidden materializado, omitido con visible siguiente,
rechazo de corrupción y de hidden omitido con superficie aún retenida; buffers,
FIFO/CRC, drain y cleanup. Build/firma correctas; staging preparado para cierre.

Resultado DIAG97: 90 imágenes visibles/CRC correctos, 99 AUs, siete hidden
enviados; cinco superficies hidden suprimidas y una omisión validada por CRC.
Fallo en visible 90, esperado `B1291A72`; recibido `6D7F7878`, exactamente CRC
del visible 89 según la fixture. No inferir por qué se repitió: puede ser
semántica de output o lifetime/correlación de superficie. Cleanup cero.
Logs privados `diag97-*`; avance parcial de 1,5 segundos, no prueba completa.

DIAG98 (`BOOT-DIAG-98-VP9-NATIVE-TAGS`) añade únicamente diagnóstico de salida
y consulta protegida de GetPictureInfo para VP9 en las primeras imágenes y
alrededor del frame 90. Guarda rc/size/valid/PTS/DTS/tag y canarios, padding,
slots offered/returned. No usa todavía esos metadatos para cambiar FIFO ni
ignora el CRC repetido; la llamada ya tiene ABI probado en AVC pero su resultado
VP9 queda por verificar. Mismo clip/configuración, no nuevas fixtures ni player.
Prueba focal host del probe y canarios con ASan/UBSan; build/firma verificadas.

Resultado DIAG98: mismo punto (90 CRC visibles), output con byte 11 = 1,
offered/returned 0/0, accepted 1. GetPictureInfo devolvió -2.128.805.631,
valid cero, PTS/DTS/tag UINT64_MAX y canarios intactos; no proporciona metadata
VP9 utilizable en esta prueba. CRC recibido es el del visible 89, no el 90.
Hay una entrada hidden posterior al visible 90 en vuelo. Esto apoya ensayar
clasificación por superficie oculta, no asumir que el siguiente output sigue
siempre FIFO de todos los AUs. Logs privados `diag98-*`; cleanup cero.

DIAG99 (`BOOT-DIAG-99-VP9-HIDDEN-ORDER`) mantiene fixture y configuración.
Cuando byte 11 marca hidden, correlaciona con una **única** entrada hidden en
vuelo aunque haya visibles anteriores pendientes. Sin hidden o con varios,
falla explícitamente por ambigüedad; no inventa tags. Las imágenes visibles
mantienen FIFO y CRC obligatorio; la omisión de hidden aún exige el oráculo
CRC del clip. No es todavía una ruta de YouTube/streaming ni una garantía de
semántica universal del flag. Host focal ASan/UBSan añade hidden que adelanta
a un visible pendiente y conserva casos de materialización, omisión, corrupción,
retención insegura y drain. Build/firma correctas, staging pendiente de cierre.

Resultado DIAG99: el propietario informó que se paró; logs confirman **fin
correcto** del probe, que mantiene el último frame en un bucle inactivo tras
cerrar el decoder. 180 visibles/180 CRC, 179 cambiados, 192 AUs, 12 hidden y
12 superframes. Once superficies hidden suprimidas y una omisión corroborada
por CRC. Error, orden y cleanup cero, dos llamadas de drain. 179 intervalos
en 3.002.968 µs (~59,61 fps), p95/máximo 17.647/28.318 µs; 80 llegadas tarde
respecto al pacing y 26 pasos lentos: integridad demostrada en este control
corto, no prueba sostenida ni objetivo de timing perfecto. GetPictureInfo
VP9 sigue sin metadata utilizable. Logs privados `diag99-*`.

Antes de integrar hidden en el player: retirar la dependencia del oráculo CRC
de referencia manteniendo reglas explícitas de flags/completion y retención.
Validar esa política en el mismo probe usando CRC solo como verificador,
incluyendo hidden sin superficie y llegada fuera de FIFO visible. No habilitar
formatos YouTube 60 fps sobre una política que depende de imágenes conocidas.

### DIAG100: política independiente de CRC, resultado explícito

`BOOT-DIAG-100-VP9-FLAG-POLICY` reutiliza la fixture de tres segundos/12 hidden.
`vp9_output_policy.hpp` selecciona metadata únicamente con tags, displayable,
indicador de superficie y retención. No recibe CRC, imágenes ni timestamps de
referencia. Superficie visible: selecciona el visible más antiguo y retira
solo hidden anteriores sin superficie retenida. Hidden materializado: exige
una única entrada hidden pendiente, incluso si adelanta a un visible. Estados
ambiguos, indicadores desconocidos y hidden retenido sin salida fallan.
CRC posterior a la selección verifica cada visible, nunca decide su metadata.
La regla de retirar hidden anteriores tras completion visible queda sometida
a validación en consola con el mismo clip; aún no habilita hidden del player.

No repite GetPictureInfo VP9, que DIAG98/99 no pudieron utilizar. Pruebas
focales ASan/UBSan: helper puro sin píxeles, FIFO con huecos, oculto posterior,
omisión, ambigüedad, flags desconocidos y retención insegura; probe integrado
con CRC, hidden materializado/omitido/adelantado, corrupción, drain y cleanup.
Build/firma correctas, staging FTP preparado. Sin fixtures nuevas.

Al acabar el probe muestra `FINALIZADA` o `ERROR`, counts de frames/CRC,
result/cleanup y duración de tres segundos. Reutiliza Canvas/font existente
en buffer lineal offscreen; solo EGL presenta, no inicia el VideoOut de Canvas.
Los logs del clip se guardan antes de la pantalla final, fuera de la cadencia
de prueba. Pendiente activación cuando el propietario cierre la app.

Resultado DIAG100: propietario confirma `FINALIZADA`. Logs: 180 visibles/180
CRC correctos, 179 cambios, 192 AUs, 12 superframes/hidden; once superficies
hidden y una omisión, error/orden/cleanup cero, dos drains. La política no
consultó CRC para seleccionar metadata. 179 intervalos en 3.002.957 µs
(~59,61 fps), p95/máximo 17.650/28.192 µs; 51 llegadas tarde y 26 pasos lentos.
Esto valida integridad y clasificación en el control acotado, no estabilidad
sostenida ni show-existing (cero en esta fixture). Logs privados `diag100-*`.
La línea de metadata nativa rc -45/guard cero/size cero es inicialización
`ENOTSUP`, no una llamada fallida nueva: GetPictureInfo VP9 ya no se consulta.
Siguiente: reutilizar la política en el stream y probar el mismo control con
hidden, audio, seek/reentrada antes de ampliar YouTube 4K60.

### DIAG101: hidden integrado en el player, control local de 12 segundos

`BOOT-DIAG-101-VP9-HIDDEN-PLAYER` reutiliza `select_vp9_output` en el stream
asíncrono del player. Cada input preserva displayable y si su superficie fue
aceptada; FIFO visible, hidden único adelantado y retiro de hidden sin
superficie tras completion visible siguen la política validada sin CRC.
El stream copia la superficie antes de release, retiene AUs pendientes y
conserva guards de layout, flags, ambigüedad y drain limitado. Reset descarta
el estado de entrada/superficie después del reset nativo. Telemetría añade
hidden materializados/omitidos y código de error de política.

Nuevo remux `hw-vp9-4k60-player-altref-tc2.mp4`: misma fixture v7, 180 paquetes
repetidos cuatro veces, 720 visibles y 48 hidden en 12 segundos, AAC 96 kbps.
Hashes de los paquetes verificados contra IVF repetido, sin reencode de vídeo.
Comando: `python3 tools/prepare-vp9-player.py --4k --fps 60 --altref --tile-columns 2`.
Control LOCAL usa este path; solo los paths de prueba explícitos activan
profundidad tres/cuatro slots. YouTube/VP9 previo sigue profundidad uno y no
se cambian formatos ni disponibilidad de 60 fps.

Host focal ASan/UBSan: stream con hidden materializado, omitido o adelantado,
PTS, drain, superficies y reset con AUs en vuelo; control visible asíncrono
para la rama compartida. Build/firma correctas; executable, etiqueta y remux
preparados por FTP. Pendiente activar tras cierre y probar continuo primero,
después pausa/seek/reentrada, conservar app abierta para logs.

Resultado continuo DIAG101: propietario confirma finalización. 768 AUs,
720 visibles recibidos/entregados, 48 hidden contabilizados (47 superficies
suprimidas y una omisión), política/decoder/cleanup cero, EOF, profundidad
tres/cuatro slots, pico pendiente cuatro y dos drains. 714 uploads, 713
intervalos en 11.995.756 µs (59,438 fps), máximo 33.686 µs, cuatro drops,
PTS válidos y sin discontinuidades. Audio 569 callbacks, 2.306.048 bytes PCM,
seis underruns y error cero. Tiempo medio de llamada decode 723 µs y copia
1.755 µs; no confundir llamada asíncrona con latencia de una imagen.
Logs privados `diag101-*`. Pendiente misma build: pausa, seek/barra y reentrada;
todavía no YouTube 4K60 ni show-existing en consola.

DIAG101 seek: propietario informa crash al +5. Logs accesibles muestran aborto
controlado del decoder por `POOL FULL` (-55), cleanup cero, no ENOMEM, seek 1/1.
78 uploads, playback 1,474 s, target 6,306 s. Última llamada sin output ni frame
aceptado, offered cero; quedaron cuatro inputs pendientes sin salida. Solo un
frame de preroll omitido. No afirmar crash OS a partir de esos logs. La priming
tras seek puede necesitar más AUs comprimidos que superficies cuando hay hidden.
Logs privados `diag101-seek-*`.

DIAG102 (`BOOT-DIAG-102-VP9-AU-POOL`) separa tamaños del pool: stream asíncrono
VP9 mantiene cuatro superficies pero conserva seis slots AU/metadata. Añade
2 MiB comprimidos, no dos superficies 4K; el límite total sigue 512 MiB y los
queries/bounds preceden cualquier asignación. FIFO/flags correlacionan seis
inputs contra cuatro superficies. Reset preserva ambos límites; ninguna AU
pendiente se sobrescribe. Errores distinguen INPUT/FRAME POOL FULL y el log
expone INPUT SLOTS. Las otras rutas mantienen sus tamaños anteriores.

Host focal ASan/UBSan `--vp9-seek-priming`: cinco llamadas sin output, primera
salida al sexto AU, input retenido sin superficie aceptada, hidden omitido,
PTS/drain/reset y dos ciclos. Verifica asignación real de seis MiB para inputs,
cuatro superficies y pico pendiente seis. Stream hidden materializado/omitido/
adelantado también pasa con límites separados. Build/firma verificadas;
solo executable/etiqueta nuevos, mismo clip de 12 segundos. Staging pendiente
de cierre confirmado; siguiente prueba +5 y reentrada en consola.

Resultado DIAG102: 49/49 seeks, EOF y error/cleanup cero. 3.570 AUs, 3.197
visibles recibidos, 1.509 preroll omitidos, 1.688 entregados; 227 hidden (177
superficies, 50 omitidos) y política cero. Profundidad tres, cuatro superficies,
seis slots AU, pico pendiente cinco: evidencia concreta de que cuatro AUs
no bastaban en este control. 1.483 uploads, 59,608 fps en intervalos activos,
máximo 18.195 µs y cero sync drops. Audio reset vídeo/audio 49/49, hold liberado,
error cero; 1.072 underruns en una prueba con 49 maniobras no caracterizan
reproducción continua ni garantizan sonido pulido. Logs privados `diag102-*`.

### DIAG103: primera prueba directa YouTube VP9 4K60 SDR

`BOOT-DIAG-103-YT-4K60` permite explícitamente formatos VP9 Profile 0 SDR
3840x2160 hasta 60 fps + AAC. El parser mantiene por defecto máximo 30 fps;
el probe de esta build activa el opt-in. Elegir 2160p prefiere el candidato
SDR de mayor fps válido; calidad automática sigue <=360p. 1440p y AVC no
amplían su límite de 30 fps, HDR/10bits/AV1/120fps permanecen excluidos.
El listado muestra fps del candidato para distinguir la prueba 2160p60.

La apertura recibe un hint explícito desde metadata seleccionada (VP9,
2160p, 31..60 fps, vídeo+audio), no deduce nada de URLs firmadas. Solo ese
formato activa profundidad tres/cuatro superficies/seis AUs; las otras
calidades mantienen su ruta previa. Hint se limpia al cerrar/cancelar, y el
guard comprueba configuración nativa real antes de iniciar. Local hidden
de 12 s permanece disponible como control, sin nueva fixture.

Host focal ASan/UBSan del selector VP9: opt-in/on/off, MP4/WebM, preferencia
60 sobre 30, hint correcto, auto unchanged, rechazo HDR/Profile2/AV1/120 y
otras alturas. Compilación nativa verifica la propagación main/open/setup;
build/firma correctas. Ejecutable preparado localmente; no transferido: FTP
no respondió y el propietario confirmó la consola apagada. No reintentar
hasta que vuelva a encenderla. La última build activada sigue siendo DIAG102.
Prueba candidata: `LXb3EKWsInQ` (Costa Rica), seleccionar 2160p/60 SDR, primero
continuo y después controles; todavía no validado con streams reales ni
show-existing en consola. No modificados exploit, payloads ni hypervisor.

DIAG103 se transfirió y activó posteriormente al encender la consola, con
DIAG102 respaldada. El propietario pidió no introducir ID ni buscar vídeos:
la interfaz aún conservaba el ID anterior y calidad auto, lo que no facilitaba
la prueba candidata.

DIAG104 (`BOOT-DIAG-104-YT-4K60-PRESET`) preselecciona `LXb3EKWsInQ` y 2160p
en la build de prueba. Al entrar en YouTube desde el selector inicia la consulta
automáticamente si el probe está idle. Tras consulta, X reproduce sin teclado
ni búsqueda. La pantalla muestra fps realmente seleccionados; para este preset
rechaza iniciar si no es VP9 2160p/31..60 fps + AAC, evitando validar 30 fps
por error. No cambia decoder, formato/policy de DIAG103 ni rutas locales.
Verificación focal: compilación nativa de preset, entrada/consulta y guard;
tests de decoder/formatos previamente pasados no repetidos. Build/firma y
tamaño de upload correctos. Sandbox de Tube5 no montado antes de sustituir;
DIAG104 activa y etiqueta verificada por FTP, DIAG103 respaldada.

Resultado directo DIAG104: propietario informa reproducción seguida de salida
del player. No llegó al final: 6,909 s de 313 s, 406 uploads/405 intervalos en
6.794.856 µs (59,604 fps), máximo 18.446 µs, cero sync drops, PTS válidos y
sin seek. Vídeo real VP9 Profile 0 3840x2160, hardware, profundidad tres,
cuatro superficies/seis AUs. Audio 326 callbacks, 1.327.104 bytes PCM, dos
underruns y error cero. No ENOMEM ni allocation failures.

Aborto explícito `VP9 OUTPUT FLAG POLICY`, código 4 = `missing_hidden`:
output nativo válido, error cero, byte 11 = 1, pero sin hidden pendiente que
la política pudiera asociar. No atribuir el caso a show-existing, corrupción
o una causa concreta sin capturar su secuencia de AUs/superficies. Decoder
cerrado/cleanup cero, sin EOF. 445 AUs, 408 visibles recibidos/entregados,
34 hidden (33 superficies + una omisión). La red de vídeo muestra CURL 42
con RX cero en la última petición; no confundir ese posible aborto de lectura
durante cierre con prueba de un fallo CDN primario. Log de audio-seek es viejo
(DIAG102), no pertenece a esta reproducción. Logs privados `diag104-*`.

Conclusión: arranque y cadencia breve de YouTube 4K60 SDR demostrados, no
reproducción completa ni estabilidad. A petición de evaluar si continuar el
proyecto tras el anuncio de ProsperoTube, se conserva el resultado de esta
prueba acotada sin cambio de código ni nueva build en este turno.

### DIAG105: corregir rechazo de salida marcada cuando solo hay visibles

El propietario pidió corregir la salida temprana. Inspección focal de los
primeros nueve segundos del stream VP9 SDR itag 315 del mismo vídeo, descargados
solo para diagnóstico privado: 540 paquetes, 45 hidden, cero show-existing.
El AU clave de 6,840 s tiene 435.373 bytes y cabecera 0x82, coincidencia exacta
con el último AU del log DIAG104. No justificar el fallo como show-existing.
El prefijo y sus logs permanecen bajo `.local/yt-vp9-fix-*`, fuera de Git.

`BOOT-DIAG-105-VP9-MARKED-VISIBLE` corrige la suposición de que byte 11 = 1
siempre exige un hidden pendiente. Cuando no hay hidden y sí visibles, asigna
la salida al visible más antiguo conservando FIFO; no fabrica entradas ni
suprime la imagen. Si hay hidden, mantiene la política anterior y sus guards
de ambigüedad, retención, flags desconocidos y metadata ausente. Los checks de
layout/capacidad/ownership y memoria no cambian. Telemetría cuenta MARKED
VISIBLE y las clases de entradas pendientes para verificar el caso en consola.

Pruebas focales ASan/UBSan: helper reproduce indicator 1 con solo visibles y
tags fuera del orden de slots; stream materializa ocho salidas marcadas con
PTS/píxeles correctos, input retenido, backpressure, drain/reset/cleanup.
El stream de hidden materializados/omitidos/adelantados sigue pasando.
Build y firma correctas, solo ejecutable/etiqueta preparados por FTP, mismo
preset automático Costa Rica 2160p60. Pendiente cierre/activación y repetir
el stream real para validar más allá de 6,9 s; no declarar reproducción completa
ni correspondencia de píxeles real validada hasta esa prueba de consola.

DIAG105 real: salió a 6,962 s, 409 uploads, 59,588 fps, sin drops/ENOMEM.
La primera protección se superó (`MARKED VISIBLE 1`); falló otra después:
policy 6 `retained_hidden`, un hidden/dos visibles pendientes. Último output
válido, error cero, padding cero, offered/returned 0/0, accepted 1; cleanup
cero. Logs privados `diag105-*`. No atribuir solución completa a DIAG105.

### DIAG106: corregir la retención obsoleta al devolver superficies

El código asociaba `frame.accepted` al input actual y dejaba `retained=true`
incluso si esa misma superficie se devolvía para una salida anterior. Esto
puede bloquear después la retirada de un hidden sin superficie, aunque su
buffer ya no esté en manos del driver. El flag de aceptación no es propiedad
permanente del AU comprimido.

`BOOT-DIAG-106-VP9-SURFACE-OWNER` registra el tag del input ligado a cada
superficie aceptada. Tras validar una salida, limpia la retención del input
asociado a la superficie que realmente se devolvió. `frame_used` **no** se
libera ahí: sigue ocupado hasta la copia/release del consumidor, o hasta
suprimir la salida hidden. Reset limpia el mapa tras reset nativo. No se
elimina el guard de retención, ni cambian pools, layout, política de flags,
red o preset de vídeo.

Host focal ASan/UBSan reproduce aceptación/devolución inmediata para un output
anterior mientras se envía hidden, luego completion visible sin superficie
hidden. Verifica metadata/píxeles, omisión, backpressure durante préstamo,
drain/reset y cleanup. También pasan stream hidden y regresión marked-visible.
Build/firma correctas; solo executable/etiqueta preparados por FTP. Pendiente
cierre/activación y validar el stream real después del punto de 6,9 segundos.

DIAG106 real: corte a 7,364 s, 433 uploads, 59,590 fps, cero drops/ENOMEM,
cleanup cero. La nueva protección de ownership superó el fallo anterior,
pero la política termina con error 5 (`missing_visible`): salida nativa válida,
error cero, byte 11 cero, un hidden y ningún visible pendiente. 472 AUs,
435 recibidos, 36 hidden (33 materializados/3 omitidos), dos marked-visible.
Logs privados `.local/console-logs/diag106-ZKWsoS`. La asociación de metadatos
no queda validada por superar los fallos anteriores; no ampliar excepciones
sin identificar las salidas reales.

### DIAG107: historial acotado antes del fallo de asociación

`BOOT-DIAG-107-VP9-OUTPUT-TRACE` mantiene la política de DIAG106 y añade un
anillo de 64 operaciones. En fallo del stream escribe únicamente números en
`/download0/tube5-vp9-output-trace.log`: tag enviado, tamaño/prefijo de cuatro
bytes del AU, metadatos/retención pendientes antes de procesar el output,
owners previos, superficie ofrecida/devuelta, aceptación, validez/indicador,
selección/máscara/error de política. Tras validar el layout calcula FNV1a64
de 64 muestras Y en los centros de una rejilla 8x8, para comparar con una
referencia software privada y distinguir imágenes visibles sin fiarse del
byte 11. Es una huella diagnóstica, no una verificación completa de píxeles.

No hay I/O por frame, ni URLs/payloads/fotogramas completos en el log. Al
reset se vacía el anillo. Formato acotado a 64 KiB, sin asignaciones dinámicas.
Host ASan/UBSan verifica wrap/truncamiento/canarios/timestamps, recorrido de
fallo de política con muestreo de superficie y cleanup; la regresión de
ownership de DIAG106 sigue pasando. Esta build instrumenta, no afirma reparar
el corte; pendiente prueba en consola con el mismo preset automático 4K60.

DIAG107 en consola repite exactamente el fallo de DIAG106: 7,364 s, 433 uploads,
59,592 fps, policy 5. Logs privados `diag107-NYEeo6`. El historial nuevo NO
se guardó: `write_failure_log` limita los snapshots a 4096 bytes y rechazaba
el reporte del anillo. La prueba de DIAG107 cubría el formato y el recorrido
de fallo, pero no la integración de formato y guardado; no aporta evidencia
adicional sobre la asociación de outputs.

### DIAG108: reparar el guardado del historial

`BOOT-DIAG-108-VP9-TRACE-WRITE` usa `write_trace_log`, con límite explícito de
64 KiB y las mismas garantías de escritura atómica/permisos numéricos del
snapshot normal, que conserva su límite de 4 KiB. No cambia reproducción,
política, instrumentación ni preset. Prueba ASan/UBSan integra un historial
real de más de 4 KiB con la escritura, lee/compara todos los bytes guardados,
comprueba permisos 0644 y rechazo por encima de 64 KiB; también pasan las
pruebas existentes de snapshots de 4 KiB. Pendiente repetir el fallo en consola
para obtener por fin el historial; no es una corrección del player.

DIAG108 obtuvo por fin el historial (`diag108-esX3nL`): mismo fallo a 7,363 s,
472 llamadas. Las 64 firmas de luma coinciden con frames visibles del prefijo
real decodificado por FFmpeg y escalado nearest-neighbor 8x8, sin re-descargar.
En torno al keyframe, eventos 444/445/446 tienen la misma firma (frames de
referencia 407/408; ambos comparten esas muestras), el evento 445 lleva byte
11=1 y no hay hidden pendiente. 447 devuelve la firma de referencia 409 y 448
la de 410. DIAG105 consumía un visible en 445; desde ahí los PTS quedan
adelantados y se retiran hidden antes de tiempo. Otra salida marcada en 461
consume el último visible y 472 falla con solo hidden pendiente. Las firmas
son muestreadas, no una prueba de igualdad de todos los píxeles.

### DIAG109: no consumir metadatos por una salida auxiliar sin hidden

`BOOT-DIAG-109-VP9-AUX-REPEAT` sustituye la excepción equivocada de DIAG105:
con indicador 1, ningún hidden y algún visible pendiente, suprime la superficie
auxiliar **sin retirar ningún AU ni PTS**. Layout/ownership se validan antes,
la superficie devuelta se libera, el input actual conserva sus bytes pendientes.
El siguiente output normal sigue el FIFO visible correcto. La política con
hidden pendiente, sus guards y la omisión de hidden anterior no cambian. Sin
metadatos o con indicadores desconocidos sigue rechazando el output. La misma
decisión se maneja en el consumidor de la prueba aislada para evitar un índice
sentinela fuera del array. Log añade `AUX SUPPRESSED` e historial `SUPPRESS`.

ASan/UBSan focal: política reproduce repetición con solo visibles, llegada de
hidden posterior y retirada solo tras los visibles anteriores; mock de stream
emite un output marcado adicional sin consumir input y luego ocho visibles,
verifica PTS/píxeles, ownership/backpressure, drain y repetición tras reset.
También pasan hidden materializado/omitido/adelantado y regresión de retención
de superficies; integración de historial en disco sigue pasando tras añadir
el campo. Sin pruebas del bridge ni cambios de red/pools/formatos. Pendiente
reproducción real más allá de los siete segundos; no declarar reparación
validada en consola hasta observar el resultado.

Dependencia local: [SDK público 1.0.1](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/v1.0.1),
archivo `ps5-opengl-sdk-1.0.1.tar.gz`, extraído en
`.deps/gpu-sdk/ps5-opengl-sdk-1.0.1/`. SHA-256 del archivo:
`aaa2e8957f55e1fc0b654dcb35a36e585f7e635d63952da40a28be7f64fde741`.
