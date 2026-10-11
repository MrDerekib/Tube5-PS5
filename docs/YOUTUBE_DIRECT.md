# DIAG42: primera petición directa a YouTube

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
Validación física de DIAG42 pendiente.

Tras validar: estudiar extracción de formatos y entrega de medios desde la
consola, incluidos flujos separados y códecs; investigar aceleración nativa
antes de comprometer 4K. No dedicar esta fase a pulir el bridge.
