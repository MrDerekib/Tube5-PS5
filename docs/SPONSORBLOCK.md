# SponsorBlock: piloto DIAG36 y pulido DIAG37

`BOOT-DIAG-36-SPONSOR` consulta intervalos en el PC y ejecuta seeks en PS5.
Solo categoría **sponsor**, acción **skip**. No recorta ni modifica el MP4.
No es un bloqueador de anuncios insertados por YouTube.

## Servicio y caché

```bash
python3 tools/http-test-server.py --bind 192.168.8.176 --port 8080 --youtube-control --sponsorblock --clip .local/youtube/qf31PkeESoU/resolution-1080p-600s.mp4
```

La consulta HTTPS usa la API oficial por prefijo SHA-256 de cuatro caracteres,
no envía el ID exacto del vídeo. El PC recibe un grupo y filtra el ID esperado.
No se envían votos, visualizaciones ni segmentos. No se consulta desde la PS5.
Una sola tarea de red en segundo plano: timeout ocho segundos, respuesta y
caché limitadas a 128 KiB, hasta ocho intervalos fusionados por entrada.
Los resultados se guardan en `.local/sponsorblock/`, no en Git ni dist.
Caché positiva y sin segmentos: 24 horas; fallo: cinco minutos antes de reintentar.
No se sirven marcas caducadas cuando el refresco falla.

`/catalog.tsv` conserva v1 para DIAG35. DIAG36 usa `/catalog-sb.tsv`, v2,
con estado y rangos en milisegundos, manteniendo ocho entradas y 4096 bytes.
Si aparece PENDIENTE en la biblioteca, esperar unos segundos y pulsar Options.
La reproducción nunca espera una consulta externa de SponsorBlock.

## Seguridad temporal

Solo los MP4 completos del preparador 360p tienen alineación habilitada.
Los recortes HD se marcan como tiempos no verificados: no basta con asumir
que sus keyframes y timestamps coinciden exactamente con el original.
Si la duración reportada de un segmento difiere en más de dos segundos del
archivo, se deshabilita ese conjunto. Se rechazan intervalos inválidos, NaN,
negativos o fuera de duración; solo sponsor/skip y votos no negativos.
Intervalos solapados/adyacentes se fusionan antes de servirlos.

El sintetico no tiene ID de YouTube y permanece sin segmentos. Los tests
usan intervalos ficticios locales, nunca los publican en SponsorBlock.
Los vídeos sin marcas o con error de red siguen reproduciéndose sin saltos.

## Controles

SponsorBlock está ON inicialmente; su preferencia dura esta sesión, todavía
no se guarda al cerrar la app. Options en el reproductor cambia ON/OFF;
en la biblioteca Options sigue recargando. En Ajustes, cruceta cambia ON/OFF,
y X conserva el selector HTTP/LOCAL. ON solo actúa si hay segmentos listos.
El HUD muestra segmentos y solicitudes de salto, no votos ni detecciones IA.
Un seek ya enviado puede terminar aunque se desactive la opción inmediatamente.

DIAG37 programa la marca que empieza en cero antes del primer draw, sin leer
el reloj aún no inicializado. El resto respeta pausa, seek pendiente y
selector de posición. Durante el salto se oculta imagen y silencia audio hasta
alcanzar el destino; se siguen consumiendo datos para sincronizar el reloj.
Una marca que alcanza el final cierra el vídeo directamente, sin reproducir
su cola. Una marca se solicita una vez por encuentro: no se
repite en bucle si el demuxer tarda o falla. Al retroceder manualmente hasta
una marca, se puede volver a saltar; desactivar SB permite ver esa sección.

## Vídeo real y prueba física

El servicio ha preparado `bHIhgxav9LY` de Veritasium, título actual
«The Biggest Misconception About Electricity»: 887,536327 s, 360p, 33.329.196 bytes.
La API devuelve sponsor 0–4 s y 809,3–887,501 s. El catálogo v2 los sirve
como `0-4000,809300-887501`, estado ready. Se descargó únicamente este vídeo
nuevo, sin variantes HD ni clips adicionales.

Con Tube5 cerrada, copiar `dist/PPSA99999/`, comprobar DIAG36 y refrescar la
biblioteca. Veritasium debe indicar SEGMENTOS LISTOS. Reproducir: debe saltar
aproximadamente al segundo cuatro y aumentar el contador. Mediante Triángulo,
seleccionar cerca de 13:25 y confirmar; al llegar a 13:29,3 debe ir al final
y volver a la biblioteca. No hace falta ver los quince minutos completos.
Reabrir con SB OFF y comprobar que no salta el inicio. Probar pausa, seek
manual y salida; las entradas sin segmentos deben conservar su comportamiento.
El propietario confirma «funciona bien» tras la prueba de DIAG36 en PS5 Fat
FW 13.20. Se registra como validación del piloto en consola; no detalló por
separado los resultados de ON/OFF, pausa, seek, salida y reapertura, por lo que
no se atribuye una comprobación exhaustiva de esas combinaciones a este reporte.

## Resultados automáticos y paquete

Pasan las suites host de media con ASan/UBSan/LSan, modelo de saltos y parser,
además de nueve tests de navegación/renderer y cuarenta de integración.
El test con segmentos reales carga el catálogo LAN y reproduce el MP4:
solicita 0,114→4 s, respeta pausa y seek manual, solicita 809,710→887 s,
llega a EOF y reabre sin errores. El destino final se limita a la duración
que expone el player (887 s), no al valor más preciso del ffprobe.
Es una prueba acelerada con seek, no quince minutos continuos de visionado.
Las capturas host de biblioteca y Ajustes se revisaron sin solapamiento de textos.

Build nativa/FSELF, integridad y guard de ABI RefStruct correctos.
SHA-256 de eboot.bin:
`59690d33db18b4d2f77023918a3deedeea82aa2f2eb8f9f2acd652bad33ec175`.

Paquete de pulido DIAG37 (mejora confirmada por el propietario en consola):
`6f55e9d0ed8a1325d24857522c1d7a3ee2d3b139816fc1bc47398c9ccb007f9a`.
Repetir el inicio y el salto desde 13:25: no deben verse los fotogramas de
patrocinio antes de empezar en 0:04 ni una cola tras volver a la biblioteca.
El resultado reportado es «mucho mejor»; no se considera confirmada la ausencia
absoluta de destellos en todas las condiciones.
Respaldo privado `.local/boot-diag-36/`; carpeta de despliegue `dist/PPSA99999/`.

## Atribución

Usa datos de [SponsorBlock](https://sponsor.ajay.app/) bajo
[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/), según
la [licencia de la API y base de datos](https://github.com/ajayyy/SponsorBlock/wiki/Database-and-API-License).
Tube5 filtra la categoría, fusiona intervalos y convierte segundos a
milisegundos: los datos transformados mantienen esa licencia, separados del
código GPL del proyecto. Ajustes y Acerca de muestran fuente y licencia.
La atribución también acompaña los datos en el endpoint LAN; no se afirma
que el autor de SponsorBlock respalde Tube5.

Referencias: [API oficial](https://wiki.sponsor.ajay.app/w/API_Docs),
[plantilla de atribución](https://gist.github.com/ajayyy/4b27dfc66e33941a45aeaadccb51de71).
