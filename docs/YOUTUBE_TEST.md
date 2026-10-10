# Primer piloto con contenido de YouTube

Este piloto descarga un vídeo en el PC, une sus pistas sin recodificar y sirve
el MP4 preparado a Tube5 por HTTP LAN. **No es streaming directo de YouTube**,
ni una integración de catálogo/búsqueda, ni SponsorBlock. El PC necesita estar
encendido. La siguiente candidata es DIAG20; no se modifican payloads ni firmware.

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
limitada a la PS5. En Tube5, seleccionar HTTP y abrir VIDEO EN RED con DIAG20.
No hay que copiar el vídeo a la consola. Esta candidata actualiza `eboot.bin`,
`build-label.txt` y `sce_sys/param.json`, con Tube5 completamente cerrada; ese
último archivo solicita 1 GiB de memoria flexible al título.
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
