# Tramas AWDL propias frente a las de iOS 26

Comparación campo a campo de lo que emite `RTLAwdInterface` (`buildAf`, `putChanSeq`, `queueData`)
con las capturas de iOS 26 de owly (`~/Desktop/AWDLLab/refs/owly/tests/captures`,
`discovery-`, `send-` y `receive-ios26.x-mt7925e-ch44-1.pcap`; 4893 action frames y 4424 tramas de
datos). En las capturas las MAC están anonimizadas (`02:00:00:00:00:0N`). Las tramas con canal en la
cabecera radiotap (44) son del iPhone; las que no lo traen son del propio nodo owly. Volcado con
`tools/awdl_af_extract.py` del laboratorio y un decodificador de TLVs ad hoc.

## 1. Cabecera fija y orden de TLVs

Cabecera de 16 B idéntica: `7f 00 17 f2 08 10 <subtipo> 00`, más el tiempo de TX físico y el
objetivo (u32 LE). PSF = subtipo 0, MIF = subtipo 3.

| Trama | iPhone (iOS 26, canal 44) | Propia |
|---|---|---|
| PSF | `4, 5, 18, 6, 24, 12, 7, 17, 21` | `4, 5, 18` + plantilla del 151 |
| MIF | `4, 5, 18, 24, 12, 7, 17, 16, 21, 6, 2×6` | `4, 5, 18` + plantilla MI de las peticiones 149/225 (desde `6fd22ef`) |

`AWDL_TX_FRAME_REVIEW.md` §5 ya comprobó que la plantilla de sincronización (petición 151) aporta
`6, 24, 12, 7, 17, 21` en ese orden. Hasta `6fd22ef` el MIF reutilizaba esa plantilla, así que le
faltaban el TLV 16 (ARPA, nombre de host) y los TLV 2 (respuestas de servicio), y el 6 iba en la
posición del PSF (H4 de esa revisión). El kernel compone esos TLV en la plantilla de *master
indication* (`buildMasterIndicationTemplate`, `AWDL_PSF_MIF_EMISSION.md` §3.2), que entrega con las
peticiones 225 (permanente) y 149 (envío inmediato). Desde `6fd22ef` cada MIF lleva esa plantilla
tras `4, 5, 18`, y el orden coincide con el del iPhone. Ni el kernel ni owly exigen esos TLV para dar
de alta al emisor, pero AirDrop necesita el ARPA y los servicios.

## 2. TLV 4 (sync params, longitud 73)

Mismos valores fijos que el iPhone: AW 16 TU (`10 00`), periodo PSF 110 TU (`6e 00`), `00 18`,
AW extendida y común 16, `03 03 03 03`, modo de presencia 4, contador de AW en +29 y alineación
AP 0, con 2 B de relleno al final.

Hay dos diferencias:

- **+15, TU restantes de la AW.** El iPhone envía 0 en sus PSF, y owly calcula lo que queda de la
  primera AW de la franja: 0 en las otras tres (`tlv.rs`, `AW_PERIOD_TU - elapsed`). El código
  enviaba lo que quedaba de cualquier AW en curso (1..16), así que un par habría visto una ventana
  abierta durante las AW de extensión. Corregido en `020c32d`: valor no nulo solo dentro de la
  primera AW de la franja.
- **Secuencia embebida.** El iPhone usa la codificación 1 (u16 por slot con marca de banda) y owly
  y el código propio usan la 3 (pares canal y clase). El parser embebido del kernel acepta las dos
  (`AWDL_TLV18_OPCLASS_PARSER.md` §4). Sin cambio.

## 3. TLV 5 y TLV 18

- TLV 5 (21 B): distancia en +3, maestro en +5, métricas del maestro en +11 y propia en +15. Mismo
  diseño que el iPhone; owly envía distancia 1 y el iPhone maestro 0.
- TLV 18 (41 B): `0f 03 00 03 ff ff` + 16 pares + 3 B de relleno, igual que el iPhone. El iPhone
  anuncia `{44,128}` en sus slots de 5 GHz y `{6,81}` en uno de 2,4 GHz; el código, `{ch,128}` o
  `{ch,81}` en los slots servidos y `{0,0}` en el resto. El kernel acepta las dos formas
  (`AWDL_TLV18_OPCLASS_PARSER.md` §6).

## 4. Tramas de datos

| Campo | iPhone | Propio |
|---|---|---|
| Control de trama | `0x08` (datos) y `0x88` (QoS, TID 0, sin A-MSDU) | `0x08` |
| A3 | `00:25:00:ff:94:73` | igual |
| Cuerpo | `aa aa 03 00 17 f2 08 00 03 04 <seq LE> 00 00 <ethertype BE>` | igual |

En las capturas no hay A-MSDU. La RX propia acepta datos y QoS, con o sin el bit de reintento
(`deliverData`, máscara `0x47` sobre el byte de flags).

## 5. Pendiente

- La plantilla real que entrega el gestor queda volcada en hexadecimal en `RTLAwdlLog` desde
  `020c32d`. Con ella se podrá comparar el TLV 21 (versión) propio con el `a0 02` del iPhone y los
  tamaños de 6, 12, 7 y 17.
- La plantilla MI que entrega el gestor (longitud y contenido) queda en `RTLAwdlLog` desde
  `6fd22ef` ("MI template N bytes"); falta verla en hardware.
