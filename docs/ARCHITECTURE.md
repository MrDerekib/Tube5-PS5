# Arquitectura de Tube5

Tube5 será una aplicación multimedia nativa para PS5. Esta primera versión es
una base de compilación y presentación: no reproduce contenido ni modifica el
cliente oficial de YouTube.

## Capas previstas

1. **Shell/UI:** navegación con mando, búsqueda, biblioteca y ajustes.
2. **Catálogo:** una interfaz de proveedor que devuelve metadatos y opciones
   de reproducción. El proveedor concreto se seleccionará sólo tras revisar
   sus términos, disponibilidad y estabilidad.
3. **Reproductor:** reproduce una URL de medio autorizada y expone tiempo,
   pausa, seek y estado. Su contrato no conoce detalles del catálogo.
4. **SponsorBlock:** consulta segmentos para el identificador de vídeo y pide
   al reproductor un seek cuando corresponde. Usará la consulta privada por
   prefijo SHA-256, almacenará preferencias localmente y mostrará atribución.

## Decisiones iniciales

- Lenguaje y runtime: C++20 sobre `ps5-native-app-boilerplate`.
- Clase PS5: media (`applicationCategoryType: 65536`).
- Identidad actual: `PPSA99999` es exclusivamente de desarrollo local. No se
  distribuirá con ella; se asignará un ID único antes del primer release.
- Licencia: GPL-3.0-or-later, heredada de la plantilla. Cualquier código que
  se tome de otro proyecto se evaluará y atribuirá antes de incorporarse.

## Límites del primer hito

El objetivo inmediato es validar una aplicación instalable, la UI básica y los
contratos de reproductor/SponsorBlock. La compatibilidad con una fuente de
vídeo concreta no se presupone ni se anuncia hasta contar con una integración
probada y permitida.
