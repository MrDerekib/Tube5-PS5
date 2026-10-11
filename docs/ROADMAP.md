# Próximos hitos

## Objetivo final y desarrollo incremental

Funcionar sin PC es obligatorio, no una mejora opcional. La búsqueda, selección,
resolución del medio, reproducción y consulta SponsorBlock deben poder hacerse
desde PS5 sin preparar ni servir vídeos desde un PC/servidor local obligatorio.
El puente actual solo ha permitido validar el player, controles, memoria y seeks;
no satisface este criterio de lanzamiento.

El PC puede seguir siendo una herramienta temporal de desarrollo. El propietario
aclara que la autonomía es el objetivo final, no una exigencia para esta fase.
Podemos prototipar búsqueda e interfaz usando el puente, manteniendo separada
esa integración para sustituirla después. No se publicará como producto final
una app que requiera una companion app en el PC.

Validaciones de autonomía necesarias antes del producto final, no prerrequisitos
que bloqueen ahora el desarrollo sobre el puente:

- [x] Probar DNS/HTTPS desde la app nativa manteniendo las protecciones de la consola (DIAG42 con nanoDNS).
- [x] Validar acceso inicial a metadatos y resolución de MP4 360p desde PS5.
- [ ] Reproducir un vídeo YouTube sin conversión ni servicio del PC; verificar
  formatos, audio/vídeo, seek y límites de memoria sobre la fuente real.
- [x] Consultar SponsorBlock desde consola sin la caché obligatoria del PC.
  DIAG45 validado por el propietario en PS5 (2026-10-11): API directa,
  saltos inicial/final, ON/OFF y marcas naranjas correctos. Los fallos de API
  no bloquean reproducción; esa degradación está probada en host, no en PS5.
- [ ] Integrar búsqueda y selección autónomas con el mando tras demostrar esa ruta.
  DIAG46 añade entrada de ID/URL y selector de calidad, pendientes de prueba
  física; búsqueda por texto todavía no es autónoma.

No se promete que cambiar los DNS baste. Evaluar y documentar los impedimentos
de autonomía durante el desarrollo para no confundir los prototipos con el
cumplimiento del objetivo final.

## Ruta prioritaria: YouTube desde PS5, no ampliar el puente

El propietario señala que se prolongó demasiado el desarrollo del puente.
No priorizar traducción de títulos ni pulido específico del servidor temporal.
Conservarlo como banco de pruebas, sin hacerlo una dependencia del producto.

1. DNS y HTTPS verificados desde la PS5; solicitar metadatos reales sin PC.
   DIAG42 validada en consola con nanoDNS: HTTP 200/CURL 0/CERT 0 y título.
2. Resolver una fuente YouTube reproducible desde consola. Inventariar los
   formatos realmente ofrecidos: resolución, fps, códec, contenedor, bitrate,
   audio y entrega conjunta/separada. No asumir que 1080p/4K ofrecen H.264
   muxado ni que una URL firmada sea permanente.
   DIAG43 inventaría formatos y comprueba 4 KiB de un MP4 AVC/AAC 360p en
   googlevideo.com. Host y consola validados (IMG_4293): HTTP 206, 4096 B,
   TLS válido y MP4 reconocido. No es reproducción aún.
3. Acceder al stream real y validar apertura, sincronización, seek, expiración
   del enlace, reconexión y memoria, sin descargar/transcodificar en el PC.
   DIAG44 integra la fuente MP4 360p con curl/AVIO, TLS verificado y caché
   acotada. Host: imagen, pausa, seeks, EOF por seek y reapertura correctos;
   PS5: propietario confirma MP4 360p, audio, pausa, seeks, salida y reapertura
   correctos (2026-10-11). Sin validar todavía expiración/reconexión ni una
   sesión prolongada completa sobre esta fuente directa.
4. Reproducción sin anuncios: identificar cómo aparecen en la ruta elegida y
   verificar su exclusión. No equiparar acceso a oEmbed o al HTML con bloqueo
   de publicidad implementado. SponsorBlock debe consultarse también desde PS5.
   DIAG45 valida sponsor/skip directo; no valida exclusión de anuncios YouTube.
5. Velocidad de reproducción con sincronización y audio adecuados; reanudación
   local por ID desde el último punto, sin confundir EOF con una pausa.
6. Inicio de sesión seguro, biblioteca personal e integración de cuenta.
   Método compatible por investigar; no almacenar credenciales o tokens en
   builds/GitHub ni prometer autenticación antes de comprobar la ruta soportada.

Investigar decodificación por hardware en paralelo al estudio de formatos.
El player actual decodifica por software. Identificar APIs accesibles en títulos
homebrew, códecs/perfiles soportados, propiedad de buffers y presentación;
hacer una prueba mínima antes de comprometer 4K o cambiar el pipeline validado.
El hardware no resuelve DNS, extracción del enlace, publicidad ni autenticación.

DIAG53: propietario confirma ciclos cortos, pausa/seeks, reproducción directa
1080p completa y reapertura sin errores. El único log FTP encontrado es el
fallo antiguo de DIAG52; el logger registra fallos gestionados, no sesiones
exitosas. Validación limitada a esta consola FW13.20 y al vídeo probado.

DIAG54: consultas VideoDec2 (módulo decimal 207), compute y requisitos de
memoria H.264 High 1080p/1440p/2160p confirmadas con retorno cero en FW13.20.
Un query exitoso NO valida decodificación, presentación ni rendimiento.
DIAG55 preparada: un frame H.264 sintético, lifecycle create/decode/reset/delete
y log FTP, sin presentación ni cambio del player. Validación PS5 pendiente.
Después: integración
1080p con audio/reloj/seeks, presentación GPU, VP9 SDR 1440p y 2160p. Conservar
software como alternativa. No asumir AV1 por hardware ni HDR antes de pruebas.
Referencias: ProsperoLight y ps5-hardware-video-decoding-research de
BlackBearReloaded; estas validaciones externas no prueban FW13.20.

### Calidad directa, no variantes preparadas en PC

Prioridad acordada: formatos separados antes de ampliar la búsqueda. NewPipe
es referencia de extracción/reproducción, no plantilla visual ni una dependencia
Android. El teclado nativo sigue pendiente; ID/URL es una entrada provisional.

Investigación 2026-10-11: `tools/probe-youtube-adaptive.py` reproduce la secuencia
visitor_id → VISIONOS player observada en NewPipeExtractor. En host, para
`bHIhgxav9LY`, devuelve 32 formatos con 32 URLs directas, frente a una URL con
el perfil ANDROID actual. Se validaron rangos MP4 de 4096 bytes, HTTP 206 y TLS
del sistema para H.264 720p (itag 136) y AAC (itag 139). No demuestra reproducción
conjunta, sincronización, autonomía PS5 ni exclusión de anuncios. La herramienta
no guarda ni imprime URLs firmadas/tokens, no descarga el vídeo completo y no
forma parte del runtime. DIAG47 porta esta extracción al cliente HTTPS nativo,
selecciona AAC default hasta 192 kb/s y sincroniza dos fuentes con reloj común.
El propietario confirma 480p y 720p en PS5. 1080p llega a audio/vídeo pero falla
con ENOMEM en buffers de trabajo del parser; DIAG49 añade un pool acotado para
las reservas identificadas. Su validación física sigue pendiente.

### Interfaz final, no menús de diagnóstico

La UI actual es un banco de pruebas. El producto necesita portada con tarjetas,
miniaturas, títulos, canal, duración y visualizaciones; resultados visuales,
foco visible y navegación fluida con mando; controles de reproducción superpuestos
y teclado nativo. Likes y otros datos solo cuando la extracción los proporcione;
sin inventar estadísticas. Portada propia sin cuenta y recomendaciones personales
son hitos distintos: estas últimas dependen de autenticación. No portar Cobalt
ni dar por disponible un navegador completo por existir componentes WebKit.

### Validaciones de calidad pendientes

- [ ] Validar DIAG46: nueva URL/ID, identidad de fuente y segmentos, selector,
  pausa/seeks/reapertura y rechazo de una calidad no compatible sin crash.
- [x] Unir vídeo y audio adaptativos desde dos fuentes HTTPS, conservando rangos,
  TLS, memoria acotada, reloj común y seek/cancelación de ambas fuentes.
- [ ] Resolver ausencia de URLs, firmas/tokens o manifiestos cuando sean
  necesarios; no presentar esos formatos como reproducibles por inventariarlos.
- [x] Validar 480p/720p separados desde YouTube en PS5.
- [x] Validar 1080p desde YouTube con DIAG53, pools de parsing/textura,
  duración completa, pausa/seeks y reapertura; no extrapolar a otros FW/códecs.
- [ ] Evaluar VP9/AV1/Opus, perfiles/fps y hardware antes de habilitar 4K.

DIAG46 Auto mantiene <=360p; manual solo permite MP4 combinado H.264/AAC con
URL HTTPS permitida hasta 1080p si realmente se ofrece. Los formatos separados
y los códecs no habilitados se muestran pendientes, sin selección reproducible.
Desde DIAG47, las alturas candidatas H.264 MP4 hasta 1080p/30 se consultan
nativamente y se combinan con AAC en reproducción (sin remux ni descarga previa).
Auto mantiene el combinado 360p. VP9/AV1/Opus, 60 fps y 4K siguen deshabilitados.
La calidad no cambia durante el play ni se persiste todavía.

### Diagnóstico de DNS para el usuario

- [ ] Investigar lectura de DNS primario/secundario del sistema con las APIs
  disponibles en el sandbox homebrew; mostrar los valores en diagnóstico si
  son accesibles. Si no lo son, indicar «DNS del sistema no disponible», sin
  inventar valores ni solicitar elevación solo para leerlos.
- [ ] Distinguir resolución fallida, timeout del resolver, respuesta 0.0.0.0/
  dirección bloqueada, conexión TCP, certificado TLS y respuesta HTTP de YouTube.
  No atribuir un fallo genérico de conexión a los DNS sin evidencia.
- [ ] Ante una respuesta DNS inutilizable para YouTube o su CDN, mostrar un
  aviso claro: «El DNS configurado no permite resolver este servidor», junto
  al dominio/etapa y resultado. Una IP concreta no demuestra por sí sola la
  política del servicio, y un DNS local puede no tener su payload activo.
- [ ] Ofrecer orientación sobre nanoDNS/configuración compatible sin cambiar
  automáticamente DNS globales, debilitar TLS o eliminar los bloqueos de Sony.
  Cubrir en pruebas DNS restrictivo, nanoDNS activo/inactivo y conectividad
  general; no usar la comprobación de PSN como requisito de Tube5.

Los puntos 2–3 tienen prototipos parciales: MP4 360p y controles validados en PS5,
pero estabilidad prolongada y expiración/reconexión pendientes. Los puntos 4–6
y la aceleración no están
validados completamente en autonomía; DIAG45 añade el prototipo SponsorBlock
directo del punto 4. El orden
puede ajustarse a impedimentos demostrados, manteniendo la autonomía prioritaria.

## Hitos restantes e historial

- [x] Validar navegación DualSense y vídeo local con audio, pausa y seek en PS5 13.20.
- [x] Abrir el piloto HTTP de YouTube a 360p y comprobar pausa, seek, salida y reapertura.
- [x] Validar clips HTTP de 720p/1080p, diez minutos continuos en 1080p y controles.
- [x] Biblioteca LAN de medios preparados en el PC, antes del proveedor definitivo.
- [x] Añadir por URL una variante en caché y validar refresco/reproducción en PS5.
- [x] Validar adquisición y publicación desde una URL nueva sin caché (360p).
- [x] Implementar búsqueda con mando y preparación mediante el puente temporal (DIAG40).
- [x] Confirmar búsqueda, selección y creación de una entrada desde PS5 (DIAG40).
- [x] Validar reproducción del nuevo resultado tras corregir el fallo RefStruct (DIAG41),
  incluidos otro vídeo, pausa, seek, salida y reapertura por reporte del propietario.
- [ ] Evaluar 4K nativo: decodificación acelerada, buffers y salida VideoOut;
  realizar después una prueba corta. No hay compatibilidad 4K confirmada.
- [ ] Elegir e integrar el proveedor de catálogo y reproducción de YouTube.
- [ ] Integrar SponsorBlock con preferencias por categoría y atribución.
- [x] Implementar piloto SponsorBlock sponsor/skip, caché y ON/OFF con atribución.
- [x] Validar el piloto DIAG36 SponsorBlock en consola por reporte del propietario.
- [x] Implementar persistencia ON/OFF y aviso discreto de salto en DIAG38.
- [x] Validar persistencia ON/OFF en consola con la corrección DIAG39.
- [x] Confirmar los avisos inicial y final de salto en consola por reporte del propietario.
- [ ] Sustituir los recursos visuales de la plantilla por identidad de Tube5.
- [ ] Elegir y comprobar un Title ID propio antes de publicar.
- [ ] Completar la auditoría de licencias del reproductor, adjuntar sus textos y
  preparar el código fuente correspondiente a las bibliotecas de cada release.
- [ ] Validar otros firmwares con colaboradores; 13.20 es el único probado.

## Donaciones antes del lanzamiento

La app seguirá siendo gratuita. Añadir una opción discreta de «Invítame a un
café» en GitHub (README y botón Sponsor mediante `.github/FUNDING.yml`) y un QR
con su enlace en «Acerca de» dentro de la app. Sin avisos al arrancar ni
interrupciones del reproductor. Plataforma y URL pendientes de elegir con el
propietario del proyecto. No usar enlaces de ejemplo en una versión publicada.
