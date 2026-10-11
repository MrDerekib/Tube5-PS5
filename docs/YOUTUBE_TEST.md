# Primer piloto con contenido de YouTube

Este piloto descarga un vídeo en el PC, une sus pistas sin recodificar y sirve
el MP4 preparado a Tube5 por HTTP LAN. **No es streaming directo de YouTube**,
ni una integración de catálogo/búsqueda, ni SponsorBlock. El PC necesita estar
encendido. El propietario confirma apertura del vídeo completo con DIAG25,
pausa, seek, salida y reapertura. No se considera validada su duración completa.
El propietario confirma imagen y audio en 720p con DIAG27 y que el clip HTTP
de 1080p se reproduce con DIAG30. Esta build incorpora descarte de preroll y
pools directos para AVBuffer, superficies SDL y entradas grandes de RefStruct.
El propietario confirma también reproducción hasta el final con salida al menú,
avance/retroceso y reapertura en DIAG30. Percibe una ligera demora adicional
al saltar, sin medida de latencia. Confirma pausa y tres ciclos correctos.
DIAG31 supera después cinco minutos completos con imagen/audio correctos,
controles y tres ciclos, con los tres pools directos regresando a cero. La prueba
de diez minutos mantiene DIAG31 y también termina correctamente en consola.
No se modifican payloads ni firmware. HTTP es la fuente predeterminada en
DIAG26; LOCAL sigue disponible en Ajustes.

## Biblioteca LAN: DIAG34

Build `BOOT-DIAG-34-LIBRARY`, funcional en consola según el propietario. Conserva el
reproductor validado y añade una lista del PC, no búsqueda directa en YouTube.
El catálogo admite hasta ocho entradas y 4096 bytes; solo crea rutas de medios
en el mismo servidor HTTP con IPv4 numérica. La carga se hace en un hilo,
con cancelación y plazo de ocho segundos.

```bash
python3 tools/http-test-server.py --bind 192.168.8.176 --port 8080 --library --clip .local/youtube/qf31PkeESoU/resolution-1080p-600s.mp4
```

Se exponen `/catalog.tsv`, `/media/network-test.mp4`, `/media/local-test.mp4`
y la ruta previa `/test-clip.mp4`. No se sirven directorios ni archivos privados.
El catálogo reutiliza el clip YouTube de diez minutos y el sintético de veinte
segundos. No requiere preparar otro vídeo ni repetir diez minutos completos.

Con Tube5 cerrada, copiar `dist/PPSA99999/` y comprobar la etiqueta DIAG34.
Entrar en Biblioteca LAN, seleccionar ambos clips y probar apertura, pausa,
selector de posición y regreso a la lista con Círculo. Options recarga la lista.
Comprobar después que se puede volver al menú y abrir de nuevo la biblioteca.

El propietario confirma que la biblioteca funciona, pero el título mixto
mostraba solo las mayúsculas: la fuente no tenía glifos para minúsculas. DIAG35
las dibuja con los glifos mayúsculos, sin modificar el ID ni los metadatos.
`ffprobe` verifica 360p en el clip sintético y el servidor corrige su altura.
La descripción usa «CLIP DE PRUEBA» y omite el falso «ID LOCAL»; una altura
ausente se muestra como resolución desconocida, nunca como 0p.
El propietario confirma en `BOOT-DIAG-35-LIBRARY` que ambos títulos se leen
correctamente y que la reproducción y las pruebas siguen funcionando.
La corrección visual queda validada en su PS5, sin regresiones observadas.

## Pruebas de resolución: DIAG26/DIAG27/DIAG28

### Prueba de duración 1080p: DIAG31

Tras pasar el minuto de 1080p y tres ciclos con DIAG30, DIAG31 conserva el
reproductor y los allocators. Añade contadores RBUF/SBUF/DBUF al menú principal
sin necesidad de provocar un error y corrige la descripción fija de «20 segundos».
Se prepara un clip de cinco minutos, con la misma normalización HD:

```bash
python3 tools/prepare-youtube-test.py 'https://youtu.be/qf31PkeESoU' --height 1080 --seconds 300
```

`--seconds` admite 60 (por defecto), 300 o 600 para HD. Se verifica la duración
solicitada con margen de un segundo, sin escalar ni cambiar perfil, fps o CRF.
Cada duración tiene su propio archivo privado; no sobrescribe las pruebas anteriores.
Los límites de tamaño crecen proporcionalmente a la duración (320 MiB para 300 s).

Archivo preparado y verificado: 1920x1080, H.264 Main YUV420 a 30 fps,
AAC-LC estéreo/44,1 kHz, 300,001814 s y 89.846.983 bytes. El servidor LAN
sirve esta variante en `http://192.168.8.176:8080/test-clip.mp4`.

Con Tube5 cerrada, copiar `dist/PPSA99999/` y verificar BOOT-DIAG-31-HTTP.
La URL se mantiene, con el servidor apuntando a `resolution-1080p-300s.mp4`.
Fotografiar los pools en el menú antes de abrir, reproducir los cinco minutos
sin saltos y revisar audio/fluidez; fotografiar al volver al menú. Los valores
vivos/KIB deberían regresar al nivel inicial; ALLOC es acumulativo y puede crecer.
Después probar pausa, saltos, salida durante reproducción y reapertura. La
validación de sesiones largas repetidas queda pendiente.

La captura posterior IMG_4287 muestra el menú con RBUF 0/32 0 KIB, SBUF 0/4
y DBUF 0/32 0 KIB, todos FAIL 0 ERR 0: los tres pools quedan sin reservas vivas
tras ese cierre. La etiqueta no se ve en el recorte. El propietario confirma
explícitamente que los cinco minutos completos terminaron bien con imagen/audio.
IMG_4288 muestra los tres pools a cero antes de reproducir, también ALLOC/FAIL/ERR
a cero: las reservas vivas retornan al nivel inicial tras el clip. Los registros
previos sí eran visibles, aunque el propietario buscaba texto amarillo. El
propietario confirma también pausa, avance/retroceso, salida anticipada y tres
ciclos correctos con los pools volviendo a cero en DIAG31.

### Siguiente duración: diez minutos sin cambiar DIAG31

```bash
python3 tools/prepare-youtube-test.py 'https://youtu.be/qf31PkeESoU' --height 1080 --seconds 600
```

Se conserva H.264 Main/YUV420 a 30 fps, CRF 23, resolución nativa 1920x1080
y AAC copiado. El clip privado es `resolution-1080p-600s.mp4` (límite 640 MiB).
No se cambia eboot ni etiqueta: la consola conserva BOOT-DIAG-31-HTTP.
El propietario confirma que los diez minutos completos finalizaron correctamente.
Tras volver al menú comunica RBUF ALLOC 872 y DBUF ALLOC 22075, «el resto a
cero». ALLOC es acumulativo, no reservas vivas. No hay captura nueva de este
resultado; se conserva como reporte textual, sin inferir el ALLOC de SBUF.

Preparado y verificado con ffprobe: 1920x1080, H.264 Main YUV420, 30 fps,
AAC-LC estéreo/44,1 kHz, 600,003628 s y 187.507.216 bytes. El servidor sirve
esta variante en la misma URL LAN. Solo se prepara el siguiente clip necesario;
no se generan tandas de resoluciones/duraciones que ya pasaron sus pruebas.
Esta preparación usa libx264 por CPU con dos hilos; no usa la iGPU del PC.
Comprobación local posterior: Radeon Vega/Renoir, renderD128 accesible fuera
del sandbox, encoder h264_vaapi presente. Una prueba sintética de dos segundos
1080p/30 con NV12/hwupload, perfil Main y nivel 4.0 termina con éxito, sin
guardar archivos. Esto confirma codificación VAAPI disponible, no equivalencia
de calidad/bitrate o rendimiento medido frente a libx264. No se cambia todavía
el preparador ni el clip activo a un encoder distinto.

### Siguiente interfaz: progreso en DIAG32

DIAG32 queda preparada sin tocar la prueba activa de diez minutos con DIAG31.
No hace falta descargar ni preparar otro clip. Tras terminar esa prueba y cerrar
Tube5, copiar `dist/PPSA99999/` y verificar BOOT-DIAG-32-HTTP. Conserva la URL
y el MP4 de diez minutos. Añade una barra de posición bajo el vídeo, amarilla
en pausa; no representa buffer ni admite scrubbing. Los controles no cambian.

Prueba breve: observar avance de barra/contador, pausar y comprobar que se
mantienen, saltar cinco segundos adelante/atrás (también en pausa), reanudar,
salir anticipadamente y reabrir. Comparar barra y contador, y comprobar pools
a cero en el menú. La confirmación física de DIAG32 queda pendiente; no es
necesario volver a ver los diez minutos completos para esta prueba de interfaz.

Resultado físico: el propietario confirma avance de barra con el vídeo, color
amarillo al pausar, avance/retroceso y salida/reapertura correctos. Confirma que
es un indicador sin interacción directa, comportamiento previsto para DIAG32.
La prueba funcional de esta interfaz queda superada.

### Selección de destino: DIAG33

Con Tube5 cerrada, copiar `dist/PPSA99999/` y comprobar BOOT-DIAG-33-HTTP.
Se reutiliza el mismo clip de diez minutos y la misma URL: no hay descargas
ni cambios de servidor. Triángulo abre/cancela el selector; cruceta mueve el
marcador ±5 s y L1/R1 ±30 s. BUSCAR muestra el destino. X confirma un salto
absoluto; círculo cancela sin salir. Fuera del selector, controles normales.

Prueba: mover el destino y comprobar que no cambia la reproducción hasta X;
cancelar y comprobar que no salta; confirmar adelante y atrás; repetir estando
en pausa (sigue pausado y la imagen se actualiza al reanudar); salir/reabrir y
comprobar que no queda un selector antiguo. Los pools deben volver a cero al
cerrar. La validación física de DIAG33 queda pendiente. No hace falta volver
a ver el clip completo.

El propietario confirma «todo ok» con DIAG33: prueba de selector superada por
reporte textual. No se cambia de clip ni se prepara una variante adicional.

Preparar un minuto de cada resolución desde sus pistas HD reales, sin ampliar
el vídeo de 360p:

```bash
python3 tools/prepare-youtube-test.py 'https://youtu.be/qf31PkeESoU' --height 720
python3 tools/prepare-youtube-test.py 'https://youtu.be/qf31PkeESoU' --height 1080
```

Estas variantes descargan únicamente el primer minuto. Las pistas disponibles
para este vídeo son H.264 a 60 fps (298/299). FFmpeg recodifica a H.264 Main,
YUV420 y 30 fps, CRF 23, sin cambiar las dimensiones; conserva el audio AAC
sin recodificar. No son pruebas de 60 fps ni del bitstream HD original.
Así se aumenta primero la resolución sin duplicar también los fps respecto
a la prueba de 360p. Archivos privados e ignorados por Git:
`resolution-720p-60s.mp4` y `resolution-1080p-60s.mp4`.
ffprobe verifica 1280x720 y 1920x1080, ambos a 30 fps, H.264 Main,
AAC-LC estéreo/44,1 kHz y 60,000363 s. Tamaños: 10.459.796 y 20.000.646 bytes.

DIAG26 amplía el máximo por AVBuffer directo a 4 MiB, con 32 slots y un máximo
de 128 MiB simultáneos, para admitir frames YUV de 1080p. No activa decodificación
por hardware. El servidor actual sirve el clip de 1080p en la URL habitual:

```bash
python3 tools/http-test-server.py --bind IP_DEL_PC --port 8080 \
  --clip .local/youtube/qf31PkeESoU/resolution-1080p-60s.mp4
```

Con Tube5 completamente cerrada, copiar `dist/PPSA99999/` y verificar
BOOT-DIAG-30-HTTP. DIAG26 tuvo audio pero ninguna imagen: falló una reserva
SDL de 3.686.400 bytes. DIAG27 usa textura STREAMING para evitar el RGB temporal
de cada actualización, sin modificar el servidor ni la calidad del clip.
Reproducir el minuto sin saltos y observar fluidez/audio;
después probar pausa, seek, salida y reapertura. Si falla, conservar los
contadores y no insistir. El fallo de seek de 1080p se corrige en DIAG28:
se decodifica pero no se entrega el tramo previo al destino desde el keyframe
anterior. Pasan el smoke local y HTTP de 1080p y la regresión HTTP de 720p;
DIAG28 falla en PS5 al crear la superficie RGB de la textura (8.294.440 bytes).
DIAG29 mueve esos píxeles a un pool directo separado, limitado a cuatro slots
y 32 MiB, con liberación al soltar la última referencia SDL.
DIAG29 supera esa etapa pero falla una reserva RefStruct de 5.234.088 bytes.
DIAG30 añade un pool separado para esa ruta concreta de la biblioteca fijada,
con liberación emparejada y un checker que bloquea builds si cambia su ABI.
El propietario confirma que ahora se reproduce en PS5 con DIAG30 (2026-10-11).
Confirma después avance/retroceso, reproducción hasta el final con salida y
reapertura sin problema. Percibe algo más de demora al saltar; no se ha medido.
Confirma también pausa y tres ciclos correctos. Quedan pendientes valoración
específica de audio/fluidez y comprobación de consumo/liberación de memoria
mediante contadores tras ciclos repetidos. La URL no cambia.
La portada todavía menciona 20 s; el contador utiliza la duración real.

El resto del documento conserva el historial de las pruebas de 360p.

## Preparación

Herramientas de desarrollo externas: yt-dlp, su runtime JavaScript compatible,
FFmpeg y ffprobe. No se instalan ni actualizan automáticamente. Consultar las
[dependencias de yt-dlp](https://github.com/yt-dlp/yt-dlp#dependencies) y su
[documentación de EJS](https://github.com/yt-dlp/yt-dlp/wiki/EJS).

```bash
python3 tools/prepare-youtube-test.py 'https://youtu.be/qf31PkeESoU'
```

El script normaliza enlaces HTTPS `youtu.be` y `/watch?v=...` de YouTube,
descarta parámetros de seguimiento y rechaza otros hosts/URLs. No usa cookies,
cuentas, configuración personal de yt-dlp ni caché global. Rechaza directos,
vídeos de menos de 20 s o más de una hora y limita el tamaño descargado. Selecciona
H.264 a un máximo de 640x360/30 fps y AAC, conservando la preferencia de idioma
del extractor. No admite enlaces arbitrarios como un proxy abierto.

Descarga y une las pistas en una carpeta temporal privada; después FFmpeg hace
un remux `-c copy -movflags +faststart`, sin modificar los códecs. ffprobe verifica
H.264 YUV420, resolución/fps, audio AAC mono/estéreo y duración. El resultado queda
en `.local/youtube/VIDEO_ID/media.mp4`, junto a metadatos mínimos sin URLs CDN
firmadas. No se sobreescribe un MP4 preparado ya existente. Todos estos archivos
están ignorados por Git, no se copian a `assets/` ni al paquete PS5 y no se
publican: el contenido del creador no forma parte de la licencia de Tube5.
Usar únicamente contenido al que se tenga acceso autorizado.

## Servirlo a la PS5

Prueba anterior: el propietario confirma reproducción correcta con DIAG23 del
extracto `diagnostic-600s.mp4`. Extracto de 600,033 s y 25.013.038 bytes, H.264 Main
640×360/YUV420/30 fps y AAC-LC estéreo/44,1 kHz, obtenido con stream copy:

```bash
ffmpeg -nostdin -v error -n -i .local/youtube/qf31PkeESoU/media.mp4 \
  -t 600 -map 0:v:0 -map 0:a:0 -c copy -movflags +faststart \
  .local/youtube/qf31PkeESoU/diagnostic-600s.mp4
python3 tools/http-test-server.py --bind IP_DEL_PC --port 8080 \
  --clip .local/youtube/qf31PkeESoU/diagnostic-600s.mp4
```

Para repetir aquella comparación, reiniciar Tube5 para liberar el bloqueo por
ENOMEM de una prueba anterior. Seleccionar HTTP con DIAG23 y abrir una sola vez,
manteniendo la misma URL. Si abre,
observar 30–60 s sin saltos; no hace falta reproducir los 10 minutos completos.
Si falla, conservar los datos del menú y no insistir. Este resultado compara
archivos, no prueba aún estabilidad prolongada ni una causa concreta.

DIAG24 falla con el completo y localiza el primer fallo en la ruta
av_buffer_alloc → av_malloc → posix_memalign, con una reserva de 368.768 bytes.
DIAG25 está compilada en `dist/PPSA99999/` y usa memoria directa para buffers
AVBuffer grandes. El servidor del puerto 8080 ya sirve `media.mp4`. Copiar la
carpeta con Tube5 cerrada, verificar DIAG25 y abrir una sola vez. Si falla,
fotografiar DBUF, FIRSTFAIL, FFALLOC y MEMFAIL; si reproduce, observar primero
30–60 s sin saltos. Procedimiento y límites:
[buffers directos](STARTUP_DIAGNOSTICS.md#boot-diag-25-http-buffers-grandes-de-ffmpeg-en-memoria-directa).

Cerrar primero la reproducción en Tube5 y después detener el servidor anterior
del puerto 8080. No cambiar archivos/servidor con una sesión HTTP abierta.
Desde la raíz del proyecto:

```bash
python3 tools/http-test-server.py --bind IP_DEL_PC --port 8080 \
  --clip .local/youtube/qf31PkeESoU/media.mp4
```

La URL de la consola sigue siendo `http://IP_DEL_PC:8080/test-clip.mp4`; el nombre
de esa ruta es un alias de pruebas, no indica que siga siendo el clip sintético.
El servidor sigue exponiendo un solo MP4, con rangos HTTP para seeks, sin mostrar
la caché, directorios ni metadatos. Se conserva la excepción del cortafuegos
limitada a la PS5. En Tube5, seleccionar HTTP y abrir VIDEO EN RED con DIAG25.
No hay que copiar el vídeo a la consola. Esta candidata actualiza `eboot.bin`,
`build-label.txt` y `sce_sys/param.json`, con Tube5 completamente cerrada; ese
último archivo conserva la solicitud de 1 GiB de memoria flexible al título.
Si falla, la pantalla mostrará `FMEM BEFORE OPEN`, `ALLOC FAIL STAGE`,
`AUDIO`, `VIDEO`, `FIRSTFAIL`, `FFALLOC` y `DBUF`.
El texto de portada aún menciona el clip de 20 s; el contador usa la duración
real del archivo. La identidad/metadatos de la fuente en la UI quedan pendientes.

Para recuperar el clip sintético, detener el proceso y arrancar el mismo servidor
sin `--clip`. No se borra ni reemplaza `assets/test-clip.mp4`.

## Vídeo elegido y validación

`qf31PkeESoU`: «El Algoritmo de TikTok Creó Un Circo Humano Moderno…».
Preparado con yt-dlp 2026.08.19, formatos 134 y 140-3 (español original):
H.264 Main, 640x360, YUV420, 30 fps; AAC-LC estéreo a 44,1 kHz.
MP4 de 73.107.469 bytes, duración ffprobe 1701,465 s (aproximadamente 28:21).
Esto amplía la prueba respecto a H.264 baseline/AAC 48 kHz del clip sintético.

El smoke usa `--real-video` para aceptar duraciones de 20 s a una hora sin
relajar las expectativas del clip sintético:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/media-smoke \
  http://IP_DEL_PC:8080/test-clip.mp4 --real-video
```

Valida diez segundos continuos, imagen no negra, pausa, seeks, salto al final,
EOF, reapertura y limpieza con sanitizadores. Pasa tanto con el MP4 local como
con el servidor LAN, incluida apertura asíncrona HTTP. **No equivale a reproducir los
28 minutos completos**, no valida sincronización audible con el backend dummy
y no confirma el backend nativo de PS5. La validación física de este contenido
falló en DIAG16: H02=0, después `HTTP READ: cannot allocate memory`, y el usuario
reporta crash normalmente al insistir tres veces. No hay backtrace ni foto del
contador MEMFAIL de ese fallo. No se considera reproducible en PS5 todavía.
No demuestra falta de RAM física, fuga confirmada ni un fallo de DNS/firewall.

DIAG17 conserva el diagnóstico MEMFAIL y READ visible en el menú tras el error.
Si la lectura devuelve ENOMEM, cierra el player y no permite otra apertura
(LOCAL o HTTP) hasta cerrar/reabrir Tube5. Es una mitigación conservadora del
patrón de reintentos, **no una corrección demostrada de la causa**. En PC se
inyecta el resultado ENOMEM después de reproducir imagen, se comprueba limpieza,
rechazo de tres reintentos y recuperación con una instancia nueva, con
sanitizadores. No equivale a agotar el allocator nativo o reproducir el crash.

Para separar duración/tablas del contenedor de códecs/contenido inicial, se
prepara un extracto privado de los primeros 120 s sin recodificar:

```bash
ffmpeg -nostdin -v error -n -i .local/youtube/qf31PkeESoU/media.mp4 \
  -t 120 -map 0:v:0 -map 0:a:0 -c copy -movflags +faststart \
  .local/youtube/qf31PkeESoU/diagnostic-120s.mp4
python3 tools/http-test-server.py --bind IP_DEL_PC --port 8080 \
  --clip .local/youtube/qf31PkeESoU/diagnostic-120s.mp4
```

Mantiene H.264 Main 640x360/AAC-LC 44,1 kHz; 120,033 s y 5.111.569 bytes.
El archivo completo permanece intacto. Un cambio de resultado solo orientaría
el diagnóstico; no prueba por sí solo qué asignación o recurso provoca el fallo.
Antes de probar, actualizar eboot.bin y build-label.txt a DIAG17 con la app
cerrada. Abrir **una vez**; si falla, fotografiar el menú entero (error, MEMFAIL
y READ), no insistir. DIAG18 mejora el audio al conservar las muestras de cada
bloque; el usuario confirma avances/retrocesos y salida. IMG_4281 muestra U0,
E0 y MEMFAIL0 en las muestras revisadas, pero no valida el archivo completo.

El completo vuelve a fallar en DIAG18 al reabrir la app: READ ENOMEM, MEMFAIL118,
ALIGN368768B ERR12 y READ142/144 ERR-12. DIAG19 reduce colas de audio y limita el
probing HTTP, conservando la corrección PCM y el bloqueo de reintentos. Véanse
[los detalles y las limitaciones](STARTUP_DIAGNOSTICS.md#boot-diag-19-http-presupuesto-de-probing-y-colas).
Probar primero apertura y 30–60 s del completo; solo si pasa iniciar una prueba
larga continua. No hace falta verlo activamente, pero saltar al final no valida
la estabilidad de toda la reproducción. No se ha confirmado todavía una fuga
ni la causa exacta del agotamiento del allocator.

La siguiente fase debe estudiar resolución de enlaces normales, entrega de los
flujos sin descarga completa previa y audio/vídeo separados. No se presume que
este puente de desarrollo sea la arquitectura definitiva o que pueda eliminarse
sin implementar sus funciones en otro componente.
