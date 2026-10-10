# Prueba HTTP en la red local

BOOT-DIAG-16-HTTP conserva la reproducción local de DIAG12 y permite elegir HTTP
en Ajustes. No es aún integración con YouTube, HTTPS, DASH ni buffering adaptativo.

## Servidor del PC

Desde la raíz del proyecto, con la IP **del PC**, no la de la PS5:

```bash
python3 tools/http-test-server.py --bind IP_DEL_PC --port 8080
```

Se mantiene en primer plano; Ctrl+C lo detiene. No se instala autoinicio ni se
cambia el firewall. El PC debe estar encendido en la misma LAN que la consola.
Si el puerto no es accesible desde otro dispositivo, comprobar aislamiento Wi-Fi
y firewall; no abrir puertos en el router ni exponer este servidor a Internet.
Sin `--bind`, escucha solo en localhost para pruebas de PC.

Si se usa UFW con entrada bloqueada, permitir únicamente la IP de la consola
al puerto del servidor, sin desactivar el cortafuegos:

```bash
sudo ufw allow from IP_DE_LA_PS5 to IP_DEL_PC port 8080 proto tcp
sudo ufw status verbose
```

Esta excepción es persistente y debe actualizarse si cambian las IP. Para
retirarla al terminar, repetir la regla con `ufw delete allow` en lugar de
`ufw allow`. El script del servidor no modifica reglas por sí mismo.

Sirve únicamente `/test-clip.mp4`, desde `assets/test-clip.mp4`. No sirve la raíz
del proyecto, listados, grabaciones MOV ni ningún otro archivo. No requiere
dependencias Python externas. Implementa GET/HEAD, Content-Length, Accept-Ranges,
rangos individuales cerrados/abiertos/sufijo, respuestas 206 y 416. Las solicitudes
se registran en la terminal; un cierre del cliente durante un seek es normal.
Es un servidor de pruebas sin autenticación ni TLS: otros equipos de la LAN que
puedan alcanzar ese puerto también pueden descargar el clip sintético.

## Configuración y copia a PS5

Crear `.local/network-test-url.txt` con una sola línea:

```text
http://IP_DEL_PC:8080/test-clip.mp4
```

La configuración local está ignorada por Git. `tools/build.sh` la copia como
`dist/PPSA99999/network-test-url.txt`; se puede editar directamente en la consola
si la IP del PC cambia, sin recompilar. No se publica una IP de LAN por defecto.
La app lee el archivo al arrancar y admite HTTP para esta prueba; sin URL, el modo
local sigue disponible y la prueba de red muestra un error de configuración.

Cerrar Tube5. Si ya está instalada DIAG12 completa, copiar estos tres archivos a
`/data/homebrew/PPSA99999/`:

- `dist/PPSA99999/eboot.bin`
- `dist/PPSA99999/build-label.txt`
- `dist/PPSA99999/network-test-url.txt`

Arrancar Tube5 y comprobar BOOT-DIAG-16-HTTP. Por defecto usa LOCAL para preservar
la comparación. En Ajustes, X alterna LOCAL/HTTP; se muestra la URL configurada.
Círculo vuelve al menú y la tarjeta cambia a VIDEO EN RED. X abre el clip HTTP.
La selección no persiste al reiniciar la app.

Primero esperar el final sin botones. Luego probar pausa, ±5 s, salir y reapertura.
Grabar el HUD y comparar LOOP, FR, SYNC DROP, MEMFAIL y YUV con DIAG12. Comprobar
también las peticiones del servidor: no confundir una reproducción local con HTTP.
Un cliente puede leer por adelantado; este clip pequeño no prueba comportamiento
con red lenta, cortes prolongados, alta resolución o pistas separadas.

## Límites y validación

`Kit_Init(KIT_INIT_NETWORK)` habilita la inicialización de red de FFmpeg.
La apertura HTTP añade `rw_timeout=5000000` (5 s por operación de E/S) y limita
protocolos a `http,tcp`. No cambia las aperturas de archivos locales ni sobrescribe
opciones explícitas del llamador. La unidad es la documentada en
[FFmpeg Protocols](https://ffmpeg.org/ffmpeg-protocols.html#Protocol-Options).
DIAG15 añade además un plazo cooperativo global de apertura/probing de 8 s.
Desde DIAG16, la apertura y probing HTTP ocurren en un hilo auxiliar; la creación
del decodificador, textura y dispositivo de audio permanece en el hilo de UI.
Esa finalización todavía puede causar una breve pausa. No hay rebuffering propio.

DIAG13 se bloqueó en PS5 al abrir HTTP, aunque el modo local siguió funcionando.
El ELF importaba funciones de resolución desde libScePosixForWebKit. La plantilla
ya documenta que esas funciones necesitan adaptación en un título nativo:
`examples/update-check/console_curl.c`. Sin backtrace, sigue siendo una causa
probable, no una identificación confirmada de la instrucción del crash.

DIAG14 aplica un adaptador derivado de esa plantilla: getaddrinfo/freeaddrinfo,
gai_strerror, getnameinfo y gethostbyname dejan de ser imports de WebKit. Para
esta fase admite **solo IPv4 numérico y puerto numérico**; los nombres de dominio
se rechazan sin consultar DNS. No cambiar los DNS de la consola para esta prueba.
Conserva el layout sockaddr_in del SDK (incluido sin_len) y la ABI FreeBSD de
getnameinfo. El wrapper fcntl respeta llamadas sin argumento y, únicamente ante
EINVAL en un socket comprobado, usa SO_NBIO de PS5 para flags no bloqueantes.
Errores de archivos normales y EBADF no se convierten en éxitos. También adapta
strcasestr (tokens ASCII) e isatty para evitar imports auxiliares de WebKit.

Al abrir HTTP, los avisos `HTTP H01: abrir conexion` y `HTTP H02: apertura retorno`
delimitan avformat_open_input. H02 muestra su retorno hexadecimal: cero significa
apertura correcta, no que el posterior probing o la reproducción ya hayan
terminado. Solo se notifican al abrir, no durante lecturas ni seeks; los avisos
de diagnóstico añaden una breve espera antes de iniciar el reloj del vídeo.
Si vuelve a bloquearse, grabar el último aviso además del error de la consola.

DIAG14 dejó de mostrar el crash, pero quedó bloqueada en H01 y requirió cierre
forzado. El servidor seguía respondiendo a las comprobaciones del PC, sin una
petición HTTP registrada de la PS5 para ese intento. No hay backtrace del bloqueo.
El fallback fcntl anterior solo intervenía ante EINVAL; por sí solo no verificaba
el modo real de un socket cuando libc devolvía éxito.

DIAG15 fuerza SO_NBIO=1 antes de connect y comprueba que el socket devuelve ese
estado. Si falla o sigue bloqueante, devuelve error sin llamar a connect. Los
traces de red no duermen: H03 indica dirección IPv4 preparada, H10 preparación
del socket, H11 entrada en connect y H12 su retorno/errno. Un retorno -1 con
EINPROGRESS es normal para una conexión no bloqueante; no demuestra fallo de red.

Además instala AVIOInterruptCB con un plazo de 8 s que cubre apertura y probing
de la fuente. Conserva un callback previo del llamador y desactiva el plazo antes
de iniciar reproducción, para no cortarla a los 8 s. Mantiene el timeout por
operación de 5 s. Muestra el error de FFmpeg al volver al menú. El límite es
cooperativo: FFmpeg debe volver a comprobar el callback; no puede interrumpir una
llamada del kernel que se quede bloqueada. Por eso también se fuerza/verifica el
socket. No se promete que el bloqueo físico esté resuelto sin probar DIAG15.

Pruebas:

```bash
python3 -m unittest discover -s tests -p test_http_test_server.py -v
bash tools/test-host-media.sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/media-smoke http://IP_DEL_PC:8080/test-clip.mp4
```

El smoke acepta URL o archivo y comprueba imagen no negra, reproducción continua,
pausa, seeks ejecutados, EOF, reapertura y limpieza con sanitizadores. Los tests
del servidor verifican bytes y rangos, HEAD, rangos inválidos y rechazo de rutas
ajenas. Un mock comprueba timeout, lista de protocolos, opciones preexistentes y
que las aperturas locales no cambien. Estas pruebas en PC no validan el backend
de sockets ni los permisos de red de la PS5; la prueba física sigue pendiente.

Validación de DIAG13-HTTP en PC: pasan los tests de rangos/aislamiento, mock de
opciones y el smoke completo tanto con archivo local como con la URL del servidor
LAN, con AddressSanitizer/UndefinedBehaviorSanitizer. El servidor registra nuevas
peticiones 206 durante los seeks y la reapertura. Compilan el preview offscreen
y la build nativa; la integridad del FSELF valida y el ELF confirma que
Kit_CreateSourceFromUrl llama al wrapper de apertura.

Validación de DIAG14: pasan mocks de dirección/puerto, rechazo de dominios/IPv6,
reverse numérico, flags de sockets y propagación de errores, con sanitizadores.
El smoke local y HTTP pasa enlazando el mismo adaptador de resolución y fcntl
(con layout Linux en PC); incluye pausa, saltos, EOF, reapertura y limpieza.
Compila la variante nativa y valida la integridad del FSELF. La auditoría del ELF
confirma que las siete funciones de resolución/texto corregidas ya no son imports;
ff_socket_nonblock llama a __wrap_fcntl. La prueba física PS5 sigue pendiente.

La validación de DIAG15 añade rechazo de sockets que ignoran SO_NBIO, propagación
del error de setsockopt, retorno EINPROGRESS, deadline con reloj simulado y un
servidor real que acepta HTTP pero no responde. El smoke de ese servidor debe
devolver error en menos de 11 s y volver a abrir el clip local. También se
comprueba que los traces de notificación no añadan usleep.
Estas pruebas pasan con sanitizadores, incluido el servidor detenido antes de
responder y la recuperación local. Pasa también el smoke HTTP normal con más de
ocho segundos continuos, pausa, seeks, EOF y reapertura; el plazo se desactiva
antes de reproducir. Compila DIAG15 nativa, valida el FSELF y el ELF confirma
que ff_connect_parallel llama al nuevo wrapper de connect.

Primera prueba física de DIAG15: H03, H10, H11 y H12 con connect=-1/errno=36
(EINPROGRESS en el SDK), seguido de H02 con error de apertura y timeout. Ya
devuelve un error en vez de quedar indefinidamente en H01, pero todavía no
reproduce HTTP. En el PC se encontró UFW activo, política de entrada DROP y
ninguna excepción para 8080. Se añadió una regla TCP limitada a las IP actuales
de consola y servidor; falta repetir la prueba física tras esa corrección. No
se considera confirmado que el cortafuegos fuera el único problema.

La repetición física tras abrir esa excepción funcionó sin cambiar DIAG15:
IMG_4260.MOV (24,752 s, privada y excluida de Git) muestra H02=0, el clip completo
y retorno al menú. El servidor registra GET 206 desde la IP de la PS5. En las
muestras se leen LOOP 16–17 ms, MEMFAIL 0, SYNC DROP 0 y YUV FAIL/DROP/ERR 0.
A los 16,767 s del clip se observan FR=503, READ=1340/1340 y VSEND/VDEC=506,
sin errores. Pausa, seeks y reapertura se confirman por el usuario fuera de
cámara, no por esa grabación. El firewall explica el timeout de DIAG15, no
demuestra la causa del crash previo de DIAG13.

## DIAG16: carga cancelable y fallos durante reproducción

La pantalla muestra `ABRIENDO VIDEO HTTP...` y mantiene el bucle de dibujo/eventos
mientras Kit_CreateSourceFromUrl abre y analiza la fuente. Círculo pide cancelar
mediante el callback de interrupción y muestra `CANCELANDO...` hasta que termina
el hilo. No se libera la fuente ni se inicia otro player antes de unir ese hilo.
Solo después se crean los recursos SDL de presentación en el hilo principal,
de acuerdo con las [restricciones de render de SDL](https://wiki.libsdl.org/SDL2/CategoryRender).
El modo LOCAL conserva su apertura síncrona anterior.

El callback de interrupción sigue disponible durante reproducción/cierre, pero
sin el plazo de apertura. Un wrapper de av_read_frame comunica errores HTTP de
lectura al hilo principal; EOF normal y EAGAIN no se tratan como errores. Un
final mucho antes de la duración esperada o 8 s sin subir frames mientras se
reproduce también devuelven un error. Pausa y seek reinician el reloj de espera.
Al cerrar se pide interrupción antes de unir lectores y liberar sus recursos.
Se conserva el timeout de E/S de 5 s. Son límites cooperativos, no una garantía
ante una syscall bloqueada en el backend nativo.

No reconecta ni retoma automáticamente: muestra el error en el menú y permite
reintentar con X. El clip pequeño puede quedar descargado antes de acabar;
detener el servidor después no necesariamente simula un corte observable.

Validación PC con sanitizadores: fuente HTTP que no envía cabeceras, cancelación
repetida antes/después de arrancar la conexión y servidor que entrega cabeceras
y parte de un MP4 válido antes de dejar de responder. Se verifica UI actualizable,
retorno acotado, frames visibles antes del corte y recuperación al clip local.
La validación física de DIAG16 sigue pendiente. DIAG15 tiene copia del ejecutable
y etiqueta en `.local/boot-diag-15/` para recuperar la referencia HTTP validada.

Prueba PS5: copiar eboot.bin y build-label.txt con Tube5 cerrada, reproducir HTTP
normal (incluidos pausa/seek/reapertura), y abrir con el servidor detenido para
comprobar animación de carga y cancelación con Círculo. Repetir sin cancelar para
ver el error de timeout y volver a LOCAL. Reiniciar el servidor antes de reintentar
HTTP. No cambiar IP, permisos de carpeta, DNS, payloads ni firmware.

Para probar timeout/cancelación con una conexión TCP aceptada, detener primero
el servidor normal y arrancarlo en modo de fallo explícito:

```bash
python3 tools/http-test-server.py --bind IP_DEL_PC --port 8080 --stall-headers
```

Este modo registra la solicitud y retiene todas las cabeceras/bytes durante
30 s antes de cerrar: la app debe cancelar o agotar su propio plazo antes.
Probar una apertura sin botones y otra con Círculo tras aproximadamente 1 s.
No cambiar la URL. Después detener este proceso y arrancar el comando normal
sin `--stall-headers` para recuperar la reproducción. No confundir esta prueba
con apagar el servidor: un puerto cerrado puede rechazar inmediatamente la
conexión y no ejercita una espera HTTP real.

Resultado físico de DIAG16 comunicado por el usuario: servidor apagado devuelve
H02=0xFFFFFFC3 (ECONNREFUSED, -61 en PS5) y regresa al menú. Con el servidor que
retiene cabeceras, timeout en aproximadamente 6 s y cancelación con Círculo
funcionan. Tras restaurar el servidor normal, vuelve a reproducir correctamente.
Esto valida los casos básicos, no garantiza reconexión automática ni estabilidad
de toda fuente. El [piloto YouTube preparado en PC](YOUTUBE_TEST.md) reutiliza
esta build y el servidor con `--clip`, manteniendo aparte el clip sintético.

DIAG12 permanece respaldada en `.local/boot-diag-12/`, con su eboot, etiqueta y
ZIP, para recuperar la referencia local si aparece una regresión.
