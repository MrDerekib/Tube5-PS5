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

- [ ] Probar DNS/HTTPS desde la app nativa manteniendo las protecciones de la consola.
- [ ] Evaluar acceso a metadatos y resolución de medios YouTube desde PS5.
- [ ] Reproducir un vídeo YouTube sin conversión ni servicio del PC; verificar
  formatos, audio/vídeo, seek y límites de memoria sobre la fuente real.
- [ ] Consultar SponsorBlock desde consola sin la caché obligatoria del PC.
- [ ] Integrar búsqueda y selección autónomas con el mando tras demostrar esa ruta.

No se promete que cambiar los DNS baste. Evaluar y documentar los impedimentos
de autonomía durante el desarrollo para no confundir los prototipos con el
cumplimiento del objetivo final.

## Hitos restantes e historial

- [x] Validar navegación DualSense y vídeo local con audio, pausa y seek en PS5 13.20.
- [x] Abrir el piloto HTTP de YouTube a 360p y comprobar pausa, seek, salida y reapertura.
- [x] Validar clips HTTP de 720p/1080p, diez minutos continuos en 1080p y controles.
- [x] Biblioteca LAN de medios preparados en el PC, antes del proveedor definitivo.
- [x] Añadir por URL una variante en caché y validar refresco/reproducción en PS5.
- [x] Validar adquisición y publicación desde una URL nueva sin caché (360p).
- [x] Implementar búsqueda con mando y preparación mediante el puente temporal (DIAG40).
- [ ] Validar búsqueda, selección y preparación de DIAG40 en consola.
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
