# Prueba HTTP en la red local

BOOT-DIAG-14-HTTP conserva la reproducción local de DIAG12 y permite elegir HTTP
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

Arrancar Tube5 y comprobar BOOT-DIAG-14-HTTP. Por defecto usa LOCAL para preservar
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
No es un límite global de apertura: la preparación/probing sigue siendo síncrona
y puede detener temporalmente la interfaz. Recuperación de cortes y apertura
asíncrona quedan para una fase posterior; todavía no hay rebuffering propio.

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

DIAG12 permanece respaldada en `.local/boot-diag-12/`, con su eboot, etiqueta y
ZIP, para recuperar la referencia local si aparece una regresión.
