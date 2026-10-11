# YouTube directo: DIAG45 SponsorBlock autónomo

## SponsorBlock y barra (DIAG45)

Se consulta la API directamente por HTTPS desde PS5 antes del play, sin servicio
PC: prefijo SHA-256 de cuatro caracteres, coincidencia exacta de ID, sponsor/skip,
validación de intervalos y duración real. No se envían cuentas/votos/telemetría.
La atribución CC BY-NC-SA 4.0 permanece en Ajustes/Acerca de y avisos de terceros.
Si falla o los tiempos no coinciden, el vídeo sigue disponible sin saltos y
con aviso. Los segmentos naranjas son visibles también con Options → SB OFF.
Triángulo vuelve a consultar el enlace y los segmentos. El propietario confirma
en PS5 Fat FW 13.20 (2026-10-11) consulta, saltos inicial/final, ON/OFF y marcas
correctos, sin PC. Los fallos de API siguen validados solo en host.

## Prueba actual (DIAG44)

1. Con Tube5 cerrada, copiar `dist/PPSA99999/` completa. Mantener nanoDNS activo.
2. Comprobar `BOOT-DIAG-45-YOUTUBE-SPONSOR`.
3. Ajustes → Triángulo → X consulta formatos y verifica primeros bytes.
4. Cuando MP4 SI, pulsar X otra vez: abre el vídeo de Veritasium directamente.
5. Probar 20–30 s de imagen/audio, pausa/reanudar, +/-5 s y Círculo para salir.
6. X vuelve a reproducir. Triángulo en la pantalla directa renueva el enlace.

No hace falta ver el vídeo completo. Ante fallo, fotografiar pantalla y etapa;
si aparece ENOMEM, cerrar completamente Tube5 y no insistir en esa sesión.
El contador YT HTTPS muestra REQ/FAIL/RX y caché; las URLs firmadas no aparecen.
No hay servidor PC en esta ruta, ni descarga previa/transcodificación. El
selector HTTP/local solo afecta al banco de pruebas anterior.

curl sirve rangos a AVIO/FFmpeg con 512 KiB de caché + 32 KiB AVIO, TLS con
cadena y nombre verificados y sin redirects/proxy. Se exige HTTP 206,
Content-Range exacto, bytes completos y tamaño estable en cada rango. El player
y sus pools son los validados. Cierre/cancelación interrumpe red y une los
threads antes de liberar buffers. DNS se resuelve antes de abrir el formato;
apertura/probing acotados a 8 s y cada petición a 5 s, sin reconexión automática.

Host real: imagen, diez segundos, pausa, seeks, EOF por seek y reapertura pasan;
cuatro rangos, cero fallos, 1644 KiB transferidos. Tests negativos offline y
sanitizadores pasan; no prueban sockets/memoria/sonido de la PS5. Build nativa
e integridad/guard ABI válidos. El propietario valida DIAG44 en PS5 Fat FW 13.20
(2026-10-11): MP4 360p directo, audio, pausa, seeks, salida y reapertura correctos.
Reproducción prolongada completa y expiración/reconexión todavía pendientes.

Esta primera ruta usa un solo vídeo público fijo y MP4 AVC/AAC combinado <=360p.
SponsorBlock se consulta desde consola en DIAG45, validado por el propietario;
el piloto previo de biblioteca LAN se conserva. No afirmar ausencia de anuncios
por haber recibido el MP4. Búsqueda autónoma, más vídeos/calidades, tokens,
expiración/reconexión, login y aceleración siguen pendientes.

Se evita el HTTPS nativo de FFmpeg 7.0.1 porque su implementación OpenSSL no
verifica hostname; véase [upstream](https://github.com/FFmpeg/FFmpeg/blob/n7.0.1/libavformat/tls_openssl.c).
No es aceptable resolver esto desactivando TLS o cambiar los DNS globales
automáticamente. El diagnóstico de DNS solicitado se incluye en ROADMAP,
pero aún no se leen los DNS primario/secundario de la consola.

La build local conserva su clave LAN privada: no publicar carpeta/ZIP.

## Historial (DIAG43)

## Prueba actual (DIAG43)

Con nanoDNS activo, cerrar Tube5 y copiar `dist/PPSA99999/` completa.
Comprobar `BOOT-DIAG-43-YOUTUBE-FORMATS`. Ajustes → Triángulo → X solicita
formatos del vídeo conocido y comprueba una fuente MP4 360p directamente
contra YouTube, sin utilizar el puente del PC. Fotografiar el resultado.

Esperado: consulta HTTP 200/CURL 0/CERT 0; formatos y códecs; línea STREAM
con ITAG 18 360P, HTTP 206/CURL 0/CERT 0, BYTES 4096 y MP4 SI. Repetir X y
comprobar Círculo durante la consulta. La fuente no se reproduce todavía:
solo leemos cuatro KiB, no el vídeo completo. No hay que esperar veinte minutos.
La build local conserva su clave LAN privada: no publicar carpeta/ZIP.

El protocolo experimental usa InnerTube player y un perfil Android observado
en [yt-dlp](https://github.com/yt-dlp/yt-dlp/blob/master/yt_dlp/extractor/youtube/_base.py).
No ejecuta yt-dlp, Python o un extractor JavaScript en la PS5. Las versiones
de cliente y restricciones de YouTube pueden cambiar; no se promete acceso
a todos los vídeos, cuentas, contenidos restringidos ni URL permanente.

JSON estricto/UTF-8 mediante json-c, respuesta descomprimida <=1 MiB,
profundidad <=32 y <=128 formatos. Body en heap acotado, no un MiB en el stack
del hilo SDL. Se inventarían AVC/VP9/AV1/AAC/Opus y máxima altura ofrecida.
No confundir presencia en la respuesta con disponibilidad de URL o soporte
del decoder. Solo MP4 conjunto AVC/AAC <=360p y URL directa es seleccionable;
se informa pero no se descifran signatures ni se implementa SABR/adaptive/HLS.

El CDN permitido es un subdominio válido de googlevideo.com, HTTPS/443,
ruta videoplayback, sin usuario/puerto alternativo/fragmentos. Resolución IPv4
actual de la consola, rechazando 0.0.0.0 y direcciones privadas/locales.
Sin redirecciones o proxy; verificación de cadena y nombre siempre activa.
La segunda petición pide Range 0-4095 y exige HTTP 206, Content-Range correcto,
4096 B y ftyp. La URL firmada se conserva solo en memoria y no se registra.
Cada HTTPS mantiene límites de conexión 5 s/total 10 s y cancelación; hay
dos fases DNS, con la limitación de cancelación de resolver indicada abajo.

Host: 36 formatos, una URL explícita, máxima altura 2160p; AVC 7/VP9 9/AV1 8,
AAC 7/Opus 6. Fuente itag 18 360p: HTTP 206/CURL 0/CERT 0, 4096 B y MP4 válido.
Parser/cancelación y suite host con sanitizadores comprobados; UI revisada.
Build/FSELF y guard ABI pasan. IMG_4293 confirma el resultado en consola:
HTTP 200/CURL 0/CERT 0, 36 formatos y fuente 360p HTTP 206, 4096 B, MP4 SI.
No se modifica el player validado ni se afirma reproducción/autonomía completa,
bloqueo publicitario, SponsorBlock directo o hardware decoding por este resultado.

## Historial: DIAG42, primera petición directa a YouTube

Esta prueba nativa solicita los metadatos oEmbed de `bHIhgxav9LY` directamente
a `www.youtube.com`, sin consultar ni descargar nada del puente del PC.
Todavía no extrae URLs de reproducción ni reproduce un stream YouTube directo.

## Prueba física

1. Cerrar completamente Tube5 y copiar `dist/PPSA99999/` al destino habitual.
2. Comprobar `BOOT-DIAG-42-YOUTUBE-DIRECT`.
3. Abrir Ajustes (Options desde el menú), pulsar Triángulo y después X.
4. Esperar el resultado y fotografiar la pantalla completa. Debe mostrar
   HTTP 200, CURL 0, CERT 0 y el título de Veritasium.
5. Repetir con X; Círculo cancela/vuelve a Ajustes. Comprobar que la interfaz
   continúa respondiendo durante DNS/HTTPS y que se puede salir/reabrir.

No cambiar DNS, elevar permisos ni desactivar certificados para esta primera
prueba: queremos medir la conectividad con la configuración actual. El PC solo
sirve para construir/copiar la app; el servicio LAN no participa en este botón.
No es necesario detenerlo y se conserva para comprobar el player anterior.
La build local sigue incluyendo la clave de emparejamiento de ese banco de
pruebas: no publicar su carpeta ni ZIP. La clave **no se envía a YouTube**.

## Qué comprueba

DNS IPv4 explícito con `sceNetPoolCreate`/`sceNetResolverStartNtoa`, recursos
propios de la petición y destrucción del resolver/pool antes de HTTPS. Se usan
los DNS actuales de la consola: no se fijan resolutores públicos ni una IP de
Google. Timeout de resolver 2 s, un retry; el límite real de esa API queda por
comprobar en hardware. La cancelación no interrumpe esa llamada síncrona: el
hilo termina la fase DNS acotada y evita iniciar HTTPS si fue cancelado.

libcurl/OpenSSL usan el nombre original de YouTube y la IP recién resuelta
mediante [CURLOPT_RESOLVE](https://curl.se/libcurl/c/CURLOPT_RESOLVE.html).
Esta separación permite distinguir fallos de DNS y conservar los wrappers
numéricos del player LAN validado. No evita las políticas del DNS configurado.

Certificados de la consola mediante el adaptador de la plantilla
`examples/update-check/console_curl.c`. Se exige validación de cadena y nombre
(`SSL_VERIFYPEER=1`, `SSL_VERIFYHOST=2`), HTTPS únicamente, HTTP/1.1, sin proxies,
sin redirecciones, cuentas, cookies o credenciales. SNI/Host conservan
`www.youtube.com`, no la IP numérica. No se descarga una CA de terceros.

Conexión TLS hasta 5 s y petición hasta 10 s, en un hilo SDL. Body acotado a
16 KiB y parser de título acotado a 127 caracteres de diagnóstico. Es un lector
limitado para oEmbed, no un parser completo de JSON ni el futuro extractor de
streams. Unicode se sustituye por `?`; no es trabajo de localización del puente.
El payload completo, tokens y URLs firmadas no se almacenan ni registran.

La UI distingue etapa DNS/HTTPS, código nativo DNS, IP, HTTP, error curl,
resultado de verificación TLS, tamaño y título/error. HTTP distinto de 200,
respuesta no reconocida o fallo TLS no se presentan como metadatos válidos.
En fallo DNS, HTTP 0 y CERT -1 indican que TLS no se probó.

Un DNS que bloquea/redirige YouTube puede fallar en resolución o llevar a un
servidor cuyo certificado no coincida. Un fallo de CA no autoriza a desactivar
TLS: primero inspeccionar ruta/permisos, certificados y hora de consola. La
pantalla permite distinguir estos casos; no promete compatibilidad de DNS.

## Validación actual

Parser host: título, espacios, escapes, Unicode limitado, entradas truncadas,
campos ausentes, escapes inválidos y respuesta sobredimensionada. Suite host
del player con sanitizadores, sin cambiar decoder ni sus pools. Petición real
del cliente host: DNS correcto, HTTP 200, CURL 0, CERT 0, 815 B y título
`The Biggest Misconception About Electricity`. Cancelación host comprobada.
Estos resultados no prueban los DNS, el allocator o la CA de la PS5.

Build nativa/FSELF y guard RefStruct correctos; no depende de
`libScePosixForWebKit` según la tabla NEEDED del ELF. La integración reutiliza
el adaptador comunitario con exclusiones para fcntl/dladdr/mkstemp ya provistos
por Tube5/SDK, en lugar de añadir un segundo wrapper de sockets.
Validación física DIAG42 mediante IMG_4292 en PS5 Fat FW 13.20 con nanoDNS:
IP 142.251.153.4, HTTP 200/CURL 0/CERT 0, 815 B y título correcto.
El DNS previo 45.56.67.85 responde 0.0.0.0 para ambos dominios youtube.com y
www.youtube.com, confirmado por consulta UDP directa desde PC. IMG_4291
coincide con esa respuesta; no demuestra un fallo del adaptador de Tube5.

Tras validar: estudiar extracción de formatos y entrega de medios desde la
consola, incluidos flujos separados y códecs; investigar aceleración nativa
antes de comprometer 4K. No dedicar esta fase a pulir el bridge.
