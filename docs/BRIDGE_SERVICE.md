# Servicio local de URLs de YouTube

El servicio prepara un MP4 en el PC y lo publica en la biblioteca LAN.
No es streaming directo de YouTube, búsqueda en YouTube ni SponsorBlock.
La consola conserva `BOOT-DIAG-35-LIBRARY`; no necesita otra build.

## Arranque

```bash
python3 tools/http-test-server.py --bind 192.168.8.176 --port 8080 --youtube-control --clip .local/youtube/qf31PkeESoU/resolution-1080p-600s.mp4
```

El catálogo y los medios siguen en el puerto 8080. El control de trabajos
escucha **solo en 127.0.0.1:8081**: no abrir este puerto en el firewall.
El cliente local lee el token privado, no requiere copiar contraseñas.
El PC necesita yt-dlp, ffmpeg y ffprobe para preparar vídeos nuevos.

## Añadir y consultar

Desde la raíz del proyecto:

```bash
python3 tools/add-youtube.py 'https://youtu.be/qf31PkeESoU'
python3 tools/add-youtube.py
```

Con URL se solicita preparación; sin URL se consulta el último trabajo.
Estados: `idle`, `preparing`, `ready` o `failed`. No pulsar repetidamente:
solo se admite un trabajo simultáneo. Al ver `ready`, pulsar **Options** en
la biblioteca PS5 y elegir la entrada. Mientras se prepara, las entradas
previas siguen disponibles; no se publica ningún archivo parcial.

Por defecto se prepara el vídeo completo a un máximo de 360p/30 fps, entre
20 segundos y una hora, con H.264 YUV420/AAC y límite de 256 MiB. Las variantes
HD de prueba son extractos normalizados a 30 fps, no vídeos completos:

```bash
python3 tools/add-youtube.py 'https://youtu.be/qf31PkeESoU' --height 1080 --seconds 600
```

HD admite 720/1080p y 60/300/600 segundos. No se escala una fuente de menor
resolución; puede fallar si no hay formato compatible. No se habilita 4K.
Se reutiliza una variante ya preparada y se vuelve a comprobar con ffprobe;
no se descarga de nuevo. No se habilita automáticamente la iGPU en esta fase.
La prueba inicial puede reutilizar `media.mp4` de la URL ya conocida.

## Persistencia y límites

Hasta ocho entradas: dos de prueba y seis añadidas. Los perfiles publicados
persisten en `.local/bridge/library.json`, sin rutas arbitrarias; los MP4
permanecen en `.local/youtube/`. Todo es privado y queda fuera de Git.
Un error no elimina las entradas anteriores y no se reintenta automáticamente.
Una caché corrupta se rechaza, no se sobrescribe ni se descarga encima de ella.
Todavía no hay interfaz gráfica, cola, cancelación individual ni borrado de
entradas. Ctrl+C/terminación del servidor interrumpe su preparación activa.
El servidor no recorre ni expone todos los archivos de la caché.

El control exige token local, rechaza Origin de navegador y valida Host.
No admite URLs fuera de YouTube, cookies de navegador ni cuentas. No está
diseñado para exponerse a Internet o a una LAN no confiable: los medios de
8080 no tienen autenticación y debe conservarse el firewall limitado a la PS5.

## Verificación

Las pruebas usan el clip sintético existente, no descargan vídeos: URL y
perfiles permitidos, límite de biblioteca, publicación, reutilización,
persistencia, aislamiento de caché, autenticación, fallo sin publicación y
actualización del catálogo/Range sin reiniciar el servidor. La adquisición
de una URL nueva en Internet queda pendiente de prueba real.

La suite completa pasa nueve pruebas de renderer/navegación y 37 de integración,
además de las comprobaciones de tooling. El servicio real recibe la URL
`qf31PkeESoU`, reutiliza su MP4 existente y devuelve `ready, reused: true`.
El catálogo LAN incluye una tercera entrada de 1701 segundos (28:21), 360p,
sin nueva descarga. El propietario confirma en PS5 que la lista se actualiza
y que las tres entradas se reproducen correctamente. Quedan validados el
refresco y la apertura de la entrada añadida mediante URL con caché reutilizada.
No se da por validada la reproducción continua de los 28 minutos completos.
El smoke test host de su ruta `/media/yt-qf31PkeESoU-360-60.mp4` pasa apertura,
decodificación, pausa, seeks, salto al final, reapertura y liberación. No
equivale a haber reproducido los 28 minutos continuos en la consola.
