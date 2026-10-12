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
No hay resultado de rendimiento en consola todavía.

Dependencia local: [SDK público 1.0.1](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/v1.0.1),
archivo `ps5-opengl-sdk-1.0.1.tar.gz`, extraído en
`.deps/gpu-sdk/ps5-opengl-sdk-1.0.1/`. SHA-256 del archivo:
`aaa2e8957f55e1fc0b654dcb35a36e585f7e635d63952da40a28be7f64fde741`.
