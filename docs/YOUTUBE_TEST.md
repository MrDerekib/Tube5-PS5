# Primer piloto con contenido de YouTube

Este piloto descarga un vídeo en el PC, une sus pistas sin recodificar y sirve
el MP4 preparado a Tube5 por HTTP LAN. **No es streaming directo de YouTube**,
ni una integración de catálogo/búsqueda, ni SponsorBlock. El PC necesita estar
encendido. La app conserva DIAG16; no se modifican sus payloads ni firmware.

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

Detener primero el servidor anterior del puerto 8080. Desde la raíz del proyecto:

```bash
python3 tools/http-test-server.py --bind IP_DEL_PC --port 8080 \
  --clip .local/youtube/qf31PkeESoU/media.mp4
```

La URL de la consola sigue siendo `http://IP_DEL_PC:8080/test-clip.mp4`; el nombre
de esa ruta es un alias de pruebas, no indica que siga siendo el clip sintético.
El servidor sigue exponiendo un solo MP4, con rangos HTTP para seeks, sin mostrar
la caché, directorios ni metadatos. Se conserva la excepción del cortafuegos
limitada a la PS5. En Tube5, seleccionar HTTP y abrir VIDEO EN RED con DIAG16.
No hay que copiar el vídeo a la consola ni cambiar eboot.bin o la configuración.
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
está pendiente: probar 1–2 minutos, audio, pausa/reanudación, ±5 s, salida y
reapertura; comparar MEMFAIL, SYNC DROP y LOOP con DIAG16/sintético.

La siguiente fase debe estudiar resolución de enlaces normales, entrega de los
flujos sin descarga completa previa y audio/vídeo separados. No se presume que
este puente de desarrollo sea la arquitectura definitiva o que pueda eliminarse
sin implementar sus funciones en otro componente.
