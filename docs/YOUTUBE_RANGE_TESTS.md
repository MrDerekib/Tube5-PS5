# HTTP 403 al buscar en YouTube — 2026-10-11

## Evidencia PS5

DIAG49, Veritasium bHIhgxav9LY, H.264 itag 137 (1080p) + AAC itag 140.
El propietario confirma reproducción, SponsorBlock, pausa y saltos cortos.
Después falla al confirmar un salto con X; también reporta fallo con +30 s
y dos/tres pulsaciones de cruceta. IMG_4261.HEIC muestra retorno a la pantalla
YouTube con HTTP 403, CURL 0, CERT 0. MEMFAIL 0, FIRSTFAIL/FFALLOC NONE;
RBUF/SBUF/DBUF/PBUF liberados y FAIL 0. La imagen documenta un error de lectura
gestionado, no un cierre del proceso por el sistema. No inferir que todos los
fallos posteriores tienen idéntica causa sin otra captura.

## Comparación de transporte en PC

`python3 tools/test-youtube-ranges.py --sessions 2`: dos extracciones nuevas,
96 peticiones, todas 206, rangos/tamaños correctos y bytes iguales entre variantes.
Cada sesión compara vídeo y audio en inicio, un tercio, dos tercios, vuelta
al primer décimo, final y de nuevo inicio. Las cuatro variantes comparten URL
firmada y cpn dentro de cada pista/sesión; el orden rota para reducir el sesgo.
GET/Android (ruta actual), GET/visionOS, POST/visionOS y POST/visionOS con rn.
Cada respuesta se limita a 512 KiB; no se descarga el vídeo completo ni se
guardan URLs/tokens. La prueba recibe metadatos nuevos y no reproduce la edad
exacta de la URL que falló en PS5. Usa urllib (conexiones distintas del curl de
Tube5); por sí sola no valida concurrencia, seek de FFmpeg ni el cliente nativo.

## Reproductor y conexiones de Tube5 en PC

La suite `bash tools/test-host-media.sh` pasa, incluidos fallos, cancelación,
reapertura y sanitizadores. `media-smoke --youtube-direct bHIhgxav9LY 1080
--seek-stress` añade posición 30 s, +30 s, tres +5 consecutivos, posición 300 s,
600 s, regreso a 30 s y al inicio. Comprueba reloj, nuevos frames decodificados
y salud HTTP en cada salto; después EOF mediante seek y reapertura de ambas
pistas. No reproduce íntegros los quince minutos. El backend host no ejercita
los syscalls ni los pools nativos de PS5.

Una primera ejecución salió con LeakSanitizer: 484 bytes en tres reservas de
FFmpeg, sin contexto completo ni resultado de los saltos. Se añadió salida
line-buffered al test y se repitió con ASAN_OPTIONS=fast_unwind_on_malloc=0,
sin desactivar detección de fugas. La primera repetición pasa los siete destinos:
802/856/624/1020/407/776/258 ms; vídeo REQ 36 FAIL 0, audio REQ 7 FAIL 0.
Una segunda repetición con extracción nueva también pasa: los siete destinos,
EOF y reapertura, vídeo REQ 36 FAIL 0 y audio REQ 7 FAIL 0; sin informes de
ASan, UBSan o LeakSanitizer en ambas repeticiones.
La fuga inicial no está explicada y no se atribuye al 403 por falta de evidencia.

## Interpretación y siguiente prueba

No se ha reproducido el HTTP 403 en PC. No hay evidencia de que POST, cambiar
User-Agent o rn lo corrijan. No modificar el transporte de producción basándose
únicamente en la diferencia con NewPipe. Hace falta comparar en PS5 la misma
URL/rango que acaba de fallar, identificando pista, offset, longitud y edad de
la fuente, sin imprimir tokens ni URL completa. Si todas las variantes devuelven
403, investigar autorización/renovación de URL; si una funciona, aislar método,
headers y conexión antes de habilitar recuperación acotada.

NewPipe conserva el User-Agent visionOS para esas URLs y usa POST en su
[YoutubeHttpDataSource](https://github.com/TeamNewPipe/NewPipe/blob/dev/app/src/main/java/org/schabi/newpipe/player/datasource/YoutubeHttpDataSource.java).
El parámetro range depende del tipo de fuente; no copiarlo indiscriminadamente
a nuestro AVIO progresivo. Los [PO Tokens](https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide)
dependen del cliente/contexto; un 403 aislado no prueba que sea ese el motivo.
