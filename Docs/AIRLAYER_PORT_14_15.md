# Porte de RTL8814AirLayer a macOS 14.8.9 (23J631) y 15.8.2 (24H232)

## 1. Alcance y método

Objetivo: determinar qué necesita `RTL8814AirLayer` para correr sobre las familias IO80211/Skywalk de
macOS 14.8.9 (23J631) y 15.8.2 (24H232), tomando 25G241 (25.6.0) como referencia de trabajo actual.

Artefactos analizados (identidad en los `KC.md5`/`KC.sha256` de cada directorio):

| build | KC | md5 | volcados |
|---|---|---|---|
| 23J631 | copia local `boot_23J631.kc` | `2ab1c3ac3db747642bf4cd6ac73e12f5` | `Docs/re/io80211_23J631/` |
| 24H232 | copia local `boot_24H232.kc` | `bcf4c23aa5f52f69951aea31c044f5de` | `Docs/re/io80211_24H232/` |
| 25G241 | `/System/Library/KernelCollections/BootKernelExtensions.kc` | `f489ff8be80e6013a1bf47561dcaef45` | `Docs/re/io80211_25.6.0/` |

Los dos directorios nuevos siguen el layout de `Docs/re/io80211_25.6.0/` (los seis volcados de
símbolos por fileset, `classes.txt`, los `vt_*.txt` y los hashes del KC). Se añaden a la referencia
los volcados `vt_IO80211VirtualInterface.txt`, `vt_IO80211AWDLProtocol.txt` y
`vt_AppleBCMWLANProximityInterface.txt`, que faltaban y hacen falta para el bloque AWDL.

Procedimiento y validación:

- Vtables decodificadas de cada KC con la misma aritmética de chained fixups que la referencia. La
  base se re-deriva por build de las cabeceras del collection: la vmaddr más baja es
  `0xffffff8000100000` en los tres builds, y con ella el 100 % de las celdas de las 13 vtables
  relevantes cae en símbolo definido (excluida `IOService`, cuyas celdas apuntan a funciones internas
  del kernel sin entrada en las symtabs, igual que en el volcado de referencia).
- El conducto se validó reproduciendo byte a byte los 12 volcados `vt_*.txt` ya existentes de 25.6.0
  antes de generar los de 23J631 y 24H232.
- Cada volcado de símbolos tiene tantas líneas como anuncia su symtab.
- Tamaños de instancia y constructores verificados con desensamblado headless sobre copias de las
  bases de análisis de cada KC (nada ejecutado sobre las originales).

Convención: los números de celda son los índices de los volcados `vt_*.txt` (slot × 8 = offset en
bytes desde el inicio de la vtable del objeto, `ZTV+0x10`). Los nombres de las celdas puras de una
clase abstracta salen de la vtable de la implementación Apple que las resuelve en ese build
(`AppleBCMWLANCore`, `AppleBCMWLANSkywalkInterface`/`AppleBCMWLANInfraProtocol`,
`AppleBCMWLANProximityInterface`).

## 2. Veredicto: ¿un header y un build para 23J631 y 24H232?

**No.** 23J631 (Sonoma) y 24H232 (Sequoia) necesitan cada uno su header, sus stubs y su binario:

1. El bloque WCL de `IO80211InfraProtocol` se recoloca entre los dos: los mismos métodos cambian de
   slot y además de orden relativo (los setters genéricos bajan −47/−68, p. ej. `setWCL_SCAN_REQ`
   659 → 592; los getters de estado −2/−32; los getters de canal suben +7/+12). De 24H232 a 25G241
   el bloque vuelve a moverse, +3/+10 según el subgrupo.
2. El conjunto de celdas puras de `IO80211InfraProtocol` difiere: 231 en 23J631, 191 en 24H232, 200 en
   25G241. En 23J631 el set incluye ~96 ioctls `apple80211` clásicos (`setSCAN_REQ`, `setASSOCIATE`,
   `getSSID`, `getBSSID`, `getSTATE`, `setROAM`, `setDEAUTH`, …) que una subclase debe responder
   obligatoriamente allí y que ya no existen en 24H232.
3. Varios métodos que la capa invalida no existen en 23J631: `getPLATFORM_CONFIG` y `getLogger` en el
   controller, los setters `setWCL_QOS_PARAMS`, `setINFRA_ENUMERATED`, `setBSS_BLACKLIST`,
   `setWCL_SOI_CONFIG`, `setWCL_UPDATE_FAST_LANE` en InfraProtocol, y `setMacAddress(mloAddrArray&)`.
4. El bloque propio de `IO80211VirtualInterface` arranca en el slot 446 en 23J631 y en el 460 en
   24H232; 23J631 tiene además tres métodos de mbuf que desaparecen en Sequoia
   (`outputPacket(__mbuf*, void*)`, `inputPacket(__mbuf*, packet_info_tag*)`,
   `reportTransmitStatus(__mbuf*, int, packet_info_tx*)`).
5. El bloque puro de `IO80211AWDLProtocol` tiene 62 celdas en 23J631 y 64 en 24H232: faltan
   `setAWDL_CONFIG` y el `setMacAddress(ether_addr&)` puro.
6. Hay cambios de firma (§6), por lo que ni siquiera un header «por nombre» sería seguro.

El header actual (`IO80211_25_6_0.h` + `AWDLInterface.h`) tampoco sirve para 24H232: además de los
slots, los tamaños de instancia de la cadena de interfaces difieren (§3) y la puerta estructural lo
rechazaría antes de crear nada.

Advertencia sobre la puerta: los tamaños de instancia de las ocho clases son **idénticos** entre
23J631 y 24H232, de modo que la comprobación estructural (tamaños + superclases) no distingue estos
dos builds. La lista de builds validados (`Tools/airlayer_builds.txt`) es lo que evita cargar un
binario de uno en el otro.

## 3. Tamaños de instancia y origen de los 16 bytes

Tamaños medidos sobre las tres bases de análisis y confirmados en el constructor de la meta-clase de cada build
(`OSMetaClass::OSMetaClass(char const*, OSMetaClass const*, unsigned int)`; el tercer argumento se
carga en `ecx` justo antes de la llamada):

| clase | 23J631 | 24H232 | 25G241 | Δ |
|---|---|---|---|---|
| `IOSkywalkInterface` | 0xB0 | 0xB0 | 0xB0 | — |
| `IOSkywalkNetworkInterface` | 0xD0 | 0xD0 | 0xE0 | +0x10 |
| `IOSkywalkEthernetInterface` | 0x110 | 0x110 | 0x120 | +0x10 |
| `IO80211SkywalkInterface` | 0x118 | 0x118 | 0x128 | +0x10 |
| `IO80211InfraInterface` | 0x120 | 0x120 | 0x130 | +0x10 |
| `IO80211InfraProtocol` | 0x120 | 0x120 | 0x130 | +0x10 |
| `IO80211VirtualInterface` | 0x120 | 0x120 | 0x130 | +0x10 |
| `IO80211AWDLProtocol` | 0x120 | 0x120 | 0x130 | +0x10 |
| `IO80211Controller` | 0x128 | 0x128 | 0x128 | — |

Evidencia del desensamblado (todas las direcciones son cabeceras de instrucción vistas en el
volcado headless):

- `IOSkywalkInterface`: 23J631 `0xffffff80028591c3` (`mov ecx, 0xB0h`), 24H232
  `0xffffff8002975ba3` (`mov ecx, 0xB0h`), 25G241 `0xffffff8002afb9a3` (`mov ecx, 0xB0h`).
  Meta-constructores en `0xffffff80028591ac` (23J631), `0xffffff8002975b8c` (24H232),
  `0xffffff8002afb98c` (25G241).
- `IOSkywalkNetworkInterface`: 23J631 `0xffffff80028654d1` (`mov ecx, 0xD0h`), 24H232
  `0xffffff8002983995` (`mov ecx, 0xD0h`), 25G241 `0xffffff8002b09ce9` (`mov ecx, 0xE0h`).
  Meta-constructores en `0xffffff80028654ba` (23J631), `0xffffff800298397e` (24H232),
  `0xffffff8002b09cd2` (25G241).

Los +0x10 entran solo en `IOSkywalkNetworkInterface`; las seis subclases los heredan y ni
`IOSkywalkInterface` ni `IO80211Controller` cambian. La comparación de `init(OSDictionary*)` de esa
clase en los tres builds muestra los mismos dos ivars con posiciones distintas:

| ivar escrito en `init` | 23J631 | 24H232 | 25G241 |
|---|---|---|---|
| puntero de expansión (`IOMallocTypeImpl`; dentro, dos `IOLockAlloc` en +0x10/+0x18 y un
`thread_call_allocate_with_options` en +0x28) | +0xB8 | +0xB8 | +0xC0 |
| flag de byte a cero | +0xC0 | +0xC8 | +0xD8 |

Evidencia: 23J631 `init` en `0xffffff80028655a0`, tiendas `0xffffff80028655c7`
(`mov [rbx+0xB8h], rax`) y `0xffffff800286561c` (`mov byte ptr [rbx+0xC0h], 0`); 24H232 `init` en
`0xffffff8002983a64`, tiendas `0xffffff8002983a8b` y `0xffffff8002983ae0` (+0xB8h y +0xC8h);
25G241 `init` en `0xffffff8002b09dac`, tiendas `0xffffff8002b09dd3` y `0xffffff8002b09e28`
(+0xC0h y +0xD8h).

Conclusión: los 16 bytes nuevos de Tahoe son el hueco que separa el puntero de expansión del flag
(+0xB8…+0xBF adicional y el corrimiento del byte de +0xC8 a +0xD8); ambos ivars existen ya en Sonoma
y Sequoia, con la misma función de init y solo cambia su colocación. Los bytes intermedios no se
inicializan en `init` (?). Para la capa, que declara esos ivars como padding opaco, el efecto práctico
es el tamaño de instancia que ya usa la puerta.

## 4. Vtables por build: conteos y cambios estructurales

Celdas por vtable (sin la celda nula final) y celdas `__cxa_pure_virtual`:

| clase | 23J631 | 24H232 | 25G241 | puras 23/24/25 |
|---|---|---|---|---|
| `IOSkywalkInterface` | 282 | 282 | 282 | 0/0/0 |
| `IOSkywalkNetworkInterface` | 332 | 332 | 333 | 0/0/0 |
| `IOSkywalkEthernetInterface` | 353 | 352 | 353 | 0/0/0 |
| `IO80211SkywalkInterface` | 446 | 460 | 461 | 0/1/0 |
| `IO80211InfraInterface` | 451 | 465 | 469 | 0/1/1 |
| `IO80211InfraProtocol` | 682 | 655 | 668 | 231/191/200 |
| `IO80211VirtualInterface` | 485 | 500 | 505 | 0/1/1 |
| `IO80211AWDLProtocol` | 547 | 563 | 568 | 62/64/64 |
| `IO80211Controller` | 471 | 463 | 463 | 10/12/12 |

Cambios que alteran la numeración, por transición:

- 23J631 → 24H232:
  - `IOSkywalkNetworkInterface`: reordenación con el mismo número de celdas:
    `registerNetworkInterfaceWithLogicalLink` y `deregisterLogicalLink` suben del 319/320 al 282/283
    y la celda pura contigua las acompaña; el bloque BSD (`prepareBSDInterface` …
    `reportDetailedLinkStatus`) se desplaza +2.
  - `IO80211SkywalkInterface` (+14): entra la familia de gestión de pares
    (`attachPeer`, `createPeer`, `detachPeer`, `findPeer`, `getController`, `getDataPathInterfaceStats`,
    `getDataPathPeerStats`, `getDataQueueDepth`, `getLastQueuePacketTime`, `getLastRxTimeStamp`,
    `getLastRxUnicastLinkActivityTime`, `getLastTxTimeStamp`, `getNClearTxRxLatency`, `logRxLatency`,
    `logTxLatency`, `setDebugTrafficReport`, `setUserBufferInfo`, `updateInterfaceDataStats`,
    `updatePeerDataStats`); salen `enabledBySystem`, `getCurrentBssid`, `setEnabledBySystem`,
    `setSelfMacAddr`, `willRoam` y la copia heredada de `registerNetworkInterfaceWithLogicalLink`.
  - `IO80211InfraInterface` (+14): lo mismo más `createLQMData` y `setCurrentApAddress`; salen
    además `setCurrentBssid` y `setPoweredOnByUser`.
  - `IO80211InfraProtocol` (−27): el set puro pasa de 231 a 191. Salen los ~96 ioctls clásicos
    (`setSCAN_REQ`, `setASSOCIATE`, `setDEAUTH`, `getSSID`, `getBSSID`, `getSTATE`, `setROAM`,
    `getRSSI_BOUNDS`, `getRX_RATE`, `getEXTENDED_STATS`, …) y entran ~57 métodos nuevos
    (`setWCL_QOS_PARAMS`, `setWCL_SOI_CONFIG`, `setWCL_UPDATE_FAST_LANE`, `setINFRA_ENUMERATED`,
    `setBSS_BLACKLIST`, `setWCL_ARP_MODE`, `setWCL_ASSOCIATED_SLEEP`, `setWCL_WNM_OFFLOAD/OPS`,
    `setFW_CLOCK_SOURCE`, `setTIMESYNC_*`, `setMWS_*`, `setPOWER_PROFILE`, `setIPV4_PARAMS`,
    `setIPV6_PARAMS`, `getHE_COUNTERS`, `getTIMESYNC_STATS`, `getSYSTEM_SLEEP_CONFIG`, …). El bloque
    WCL común se recoloca y reordena (§2.1).
  - `IO80211VirtualInterface`/`IO80211AWDLProtocol` (+15/+16): bloque propio de VI del 446 al 460;
    entran `getPeerSlotDataStats`, `getPeerDataStats`, `clearPeerDataStats` y
    `setAwdlCurrentChannelSequenceIndex` (celdas 467-470 de 24H232) y el resto del bloque propio se
    desplaza de 453-465 a 471-483; salen el trío de mbuf de Sonoma (`outputPacket`, `inputPacket`,
    `reportTransmitStatus`, celdas 466-468 de 23J631) y `setMacAddress(ether_addr&)` pasa a ser puro
    en el slot 415; el bloque AWDL suma `setAWDL_CONFIG` (62 → 64 puras con él y el `setMacAddress`).
  - `IO80211Controller` (−8): desaparece el dispatcher ioctl clásico
    (`apple80211_ioctl`/`_get`/`_set`, `apple80211SkywalkRequest`, `apple80211VirtualRequest`),
    `enable`/`disable(IO80211SkywalkInterface*)`, `postMessage`,
    `getHardwareAddressForInterface`, `useAppleRSNSupplicant`, `isAssociatedToMovingNetwork` y
    `setVirtualHardwareAddress`; entran `CreatePostOffice`, `getPostOffice`, `debugStateInit`,
    `getActionFramePoolCapacity`, `getPLATFORM_CONFIG` y `allocIO80211RecursiveLock`. Las puras
    pasan de 10 a 12: `handleCardSpecific` baja de 408 a 403, el bloque de getters de 413-420 a
    408-415, y `getLogger` (424) y `getFaultReporterFromDriver` (432) aparecen como puras.
- 24H232 → 25G241:
  - `IOSkywalkNetworkInterface` (+1): `setRxFlowSteering` (~293); además `willTerminate` pasa a estar
    invalidado en la clase (slot 128, sin cambio de conteo).
  - `IO80211SkywalkInterface` (+1): `setRxDataStallStats`, `setRxFlowSteering`, `syncDPSStats`; el
    `setLinkState` de 4 argumentos sale (su celda la ocupan `isChipInterfaceReady`/
    `isDebounceOnGoing`).
  - `IO80211InfraInterface` (+4): lo anterior más `setWCL_LINK_STATE_UPDATE` (que sale de las puras de
    InfraProtocol en Tahoe).
  - `IO80211InfraProtocol` (+13/−4 en puras): entran `getACCESSORY_STATE`, `getDEVICE_ORIENTATION`,
    `getHE_CAPABILITY`, `getLQM_STATISTICS`, `getP2P_DEVICE_CAPABILITY`, `getPOWERTABLE_VERSION`,
    `getSMARTCCA_OPMODE`, `setACCESSORY_STATE`, `setBTCOEX_EXT_PROFILE`, `setDEVICE_ORIENTATION`,
    `setMITIGATE_INTERFERENCE`, `setOS_ELIGIBILITY`, `setTX_MODE_CONFIG`; salen `set6G_MODE`,
    `setWCL_LINK_STATE_UPDATE`, `getWIFI_BT_5G_POLICY`, `setWIFI_BT_5G_POLICY`. El bloque WCL se
    recoloca +3/+10 según el subgrupo.
  - `IO80211VirtualInterface` (+5): `flushFlowQueues`, `signalMulticastPackets`, el segundo overload
    de `p2pMulticastTx`, `syncDPSStats`, `setRxDataStallStats`/`setRxFlowSteering`; el
    `setMacAddress(ether_addr&)` puro se reubica del 415 al final del bloque propio (488).
  - `IO80211AWDLProtocol`: mismas celdas que 24H232 con desplazamiento +5 uniforme
    (`getSSID` 505 … `setAWDL_RTG_PEER_STATS_CONFIG` 567).
  - `IO80211Controller`: sin cambios (463 celdas, mismas puras).

## 5. Slots de los métodos que la capa declara o invalida

Búsqueda por nombre y dueño en los volcados de cada build; las celdas puras de
`IO80211InfraProtocol`/`IO80211VirtualInterface`/`IO80211AWDLProtocol` se nombran con la vtable Apple
correspondiente del mismo build. «Firma distinta» = el nombre demangleado difiere en algún build
(detalle en §6).

### Controller (RTLAirController sobre IO80211Controller; fuente vt_IO80211Controller y vt_AppleBCMWLANCore)

| método | 23J631 | 24H232 | 25G241 | estado |
|---|---|---|---|---|
| `start` | 184 | 184 | 184 | IGUAL |
| `stop` | 185 | 185 | 185 | IGUAL |
| `free` | 18 | 18 | 18 | IGUAL |
| `getHardwareAddress` | 279 | 279 | 279 | IGUAL |
| `isCommandProhibited` | 394 | 394 | 394 | IGUAL |
| `getDRIVER_VERSION` | 413 | 408 | 408 | slot distinto |
| `getHARDWARE_VERSION` | 414 | 409 | 409 | slot distinto |
| `getCARD_CAPABILITIES` | 415 | 410 | 410 | slot distinto |
| `getPOWER` | 416 | 411 | 411 | slot distinto |
| `setPOWER` | 417 | 412 | 412 | slot distinto |
| `getCOUNTRY_CODE` | 418 | 413 | 413 | slot distinto |
| `getPLATFORM_CONFIG` | AUSENTE | 416 | 416 | AUSENTE en algún build |
| `getLogger` | AUSENTE | 424 | 424 | AUSENTE en algún build |
| `getFaultReporterFromDriver` | 436 | 432 | 432 | slot distinto |
| `createWorkQueue` | 395 | 395 | 395 | IGUAL |
| `getWorkQueue` | 396 | 397 | 397 | slot distinto |

### WCL (RTLAirInterface sobre IO80211InfraProtocol; fuente vt_IO80211InfraProtocol y vtables Apple)

| método | 23J631 | 24H232 | 25G241 | estado |
|---|---|---|---|---|
| `enable` | 266 | 266 | 266 | IGUAL |
| `disable` | 267 | 267 | 267 | IGUAL |
| `setWCL_SCAN_REQ` | 659 | 592 | 601 | slot distinto |
| `setWCL_SCAN_ABORT` | 654 | 587 | 596 | slot distinto |
| `setWCL_ASSOCIATE` | 660 | 593 | 602 | slot+firma distintos |
| `setWCL_JOIN_ABORT` | 657 | 590 | 599 | slot+firma distintos |
| `setWCL_LEAVE_NETWORK` | 647 | 581 | 590 | slot distinto |
| `setWCL_LINK_UP_DONE` | 662 | 595 | 604 | slot distinto |
| `setWCL_REASSOC` | 648 | 582 | 591 | slot distinto |
| `setWCL_QOS_PARAMS` | AUSENTE | 594 | 603 | AUSENTE en algún build |
| `setWCL_SET_SCAN_HOME_AWAY_TIME` | 663 | 596 | 605 | slot distinto |
| `setWCL_ROAM_PROFILE_CONFIG` | 650 | 585 | 594 | slot distinto |
| `setWCL_SET_ROAM_LOCK` | 649 | 583 | 592 | slot distinto |
| `setWCL_SOI_CONFIG` | AUSENTE | 640 | 648 | AUSENTE en algún build |
| `setWCL_REAL_TIME_MODE` | 655 | 588 | 597 | slot distinto |
| `setWCL_UPDATE_FAST_LANE` | AUSENTE | 625 | 633 | AUSENTE en algún build |
| `setROAM_PROFILE` | 605 | 552 | 562 | slot+firma distintos |
| `setBTCOEX_PROFILE` | 616 | 562 | 572 | slot distinto |
| `setBTCOEX_PROFILE_ACTIVE` | 617 | 563 | 573 | slot distinto |
| `setOS_FEATURE_FLAGS` | 672 | 604 | 612 | slot distinto |
| `setPOWERSAVE` | 569 | 538 | 548 | slot distinto |
| `setINFRA_ENUMERATED` | AUSENTE | 630 | 638 | AUSENTE en algún build |
| `setIE` | 590 | 543 | 553 | slot distinto |
| `setBSS_BLACKLIST` | AUSENTE | 572 | 582 | AUSENTE en algún build |
| `setUSB_HOST_NOTIFICATION` | 630 | 570 | 580 | slot distinto |
| `setVIRTUAL_IF_CREATE` | 593 | 546 | 556 | slot distinto |
| `getCHANNEL` | 453 | 465 | 469 | slot distinto |
| `getRSSI` | 462 | 470 | 474 | slot distinto |
| `getRATE` | 456 | 468 | 472 | slot distinto |
| `getOP_MODE` | 461 | 469 | 473 | slot distinto |
| `getNSS` | 526 | 504 | 508 | slot distinto |
| `getMCS` | 475 | 473 | 477 | slot distinto |
| `getMCS_VHT` | 491 | 476 | 480 | slot distinto |
| `getMAX_NSS_FOR_AP` | 512 | 495 | 499 | slot distinto |
| `getBEACON_INFO` | 522 | 502 | 506 | slot distinto |
| `getSUPPORTED_CHANNELS` | 464 | 471 | 475 | slot distinto |
| `getHW_SUPPORTED_CHANNELS` | 507 | 490 | 494 | slot distinto |
| `getCHANNELS_INFO` | 493 | 477 | 481 | slot distinto |
| `getCOUNTRY_CHANNELS_INFO` | 538 | 513 | 517 | slot distinto |
| `getROAM_PROFILE` | 496 | 479 | 483 | slot+firma distintos |
| `getWCL_BSS_INFO` | 550 | 520 | 524 | slot distinto |
| `getWCL_TRAFFIC_COUNTERS` | 551 | 521 | 525 | slot distinto |
| `getWCL_FW_HOT_CHANNELS` | 548 | 518 | 522 | slot distinto |
| `getWCL_LOW_LATENCY_INFO` | 549 | 519 | 523 | slot distinto |
| `getWCL_EXTENDED_BSS_INFO` | 560 | 528 | 531 | slot distinto |

### VirtualInterface, celdas heredadas redeclaradas (AWDLInterface.h)

| método | 23J631 | 24H232 | 25G241 | estado |
|---|---|---|---|---|
| `configureReport` | 133 | 133 | 133 | IGUAL |
| `updateReport` | 134 | 134 | 134 | IGUAL |
| `terminate` | 192 | 192 | 192 | IGUAL |
| `newUserClient` | 238 | 238 | 238 | IGUAL |
| `stringFromReturn` | 240 | 240 | 240 | IGUAL |
| `prepareBSDInterface` | 283 | 285 | 285 | slot distinto |
| `processBSDCommand` | 288 | 290 | 290 | slot distinto |
| `setInterfaceEnable` | 291 | 293 | 294 | slot distinto |
| `getFeatureFlags` | 308 | 310 | 311 | slot distinto |
| `bpfTap` | 313 | 315 | 316 | slot distinto |
| `setMulticastAddresses` | 337 | 336 | 337 | slot distinto |
| `createPeerManager` | 353 | 352 | 353 | slot distinto |
| `postMessage` | 354 | 354 | 355 | slot distinto |
| `logTxPacket` | 358 | 358 | 359 | slot distinto |
| `logTxCompletionPacket` | 359 | 359 | 360 | slot+firma distintos |
| `inputPacket` | 361 | 361 | 362 | slot+firma distintos |
| `logSkywalkTxReqPacket` | 363 | 363 | 364 | slot distinto |
| `pendingPackets` | 364 | 364 | 365 | slot distinto |
| `packetSpace` | 365 | 365 | 366 | slot distinto |
| `linkState` | 369 | 369 | 369 | IGUAL |
| `setScanningState` | 370 | 370 | 370 | IGUAL |
| `updateLinkParameters` | 373 | 374 | 374 | slot distinto |
| `updateInterfaceCoexRiskPct` | 374 | 375 | 375 | slot distinto |
| `setInterfaceNF` | 380 | 381 | 381 | slot distinto |
| `setDebugFlags` | 383 | 384 | 384 | slot distinto |
| `debugFlags` | 384 | 385 | 385 | slot distinto |
| `setInterfaceChipCounters` | 385 | 386 | 386 | slot distinto |
| `setInterfaceMIBdot11` | 386 | 387 | 387 | slot distinto |
| `setFrameStats` | 387 | 388 | 388 | slot distinto |
| `setInfraSpecificFrameStats` | 388 | 389 | 389 | slot distinto |
| `getWmeTxCounters` | 389 | 390 | 391 | slot distinto |
| `setWoWEnabled` | 394 | 392 | 393 | slot distinto |
| `wowEnabled` | 395 | 393 | 394 | slot distinto |
| `printDataPath` | 396 | 394 | 395 | slot distinto |
| `findOrCreateFlowQueue` | 397 | 396 | 397 | slot distinto |
| `findOrCreateFlowQueueWithCache` | 398 | 397 | 398 | slot distinto |
| `findExistingFlowQueue` | 399 | 398 | 399 | slot distinto |
| `removePacketQueue` | 382 | 383 | 383 | slot distinto |
| `flushPacketQueues` | 401 | 400 | 401 | slot distinto |
| `cachePeer` | 402 | 401 | 402 | slot distinto |
| `shouldLog` | 403 | 402 | 403 | slot distinto |
| `vlogDebug` | 404 | 403 | 404 | slot distinto |
| `vlogDebugBPF` | 405 | 404 | 405 | slot distinto |
| `getP2PSkywalkPeerMgr` | 408 | 407 | 408 | slot distinto |
| `findPeer` | AUSENTE | 409 | 410 | AUSENTE en algún build |
| `getEventPipeSize` | 440 | 439 | 439 | slot distinto |
| `createEventPipe` | 441 | 440 | 440 | slot distinto |

### VirtualInterface, bloque propio (AWDLInterface.h; dueño IO80211VirtualInterface)

| método | 23J631 | 24H232 | 25G241 | estado |
|---|---|---|---|---|
| `init` | 446 | 460 | 461 | slot distinto |
| `setLinkState` | 447 | 461 | 462 | slot distinto |
| `forwardPacket` | 448 | 462 | 463 | slot distinto |
| `handleIoctl` | 449 | 463 | 464 | slot distinto |
| `addPeerToCache` | 450 | 464 | 465 | slot distinto |
| `deletePeerFromCache` | 451 | 465 | 466 | slot distinto |
| `handleDebugCmd` | 452 | 466 | 467 | slot distinto |
| `getPeerSlotDataStats` | AUSENTE | 467 | 468 | AUSENTE en algún build |
| `getPeerDataStats` | AUSENTE | 468 | 469 | AUSENTE en algún build |
| `clearPeerDataStats` | AUSENTE | 469 | 470 | AUSENTE en algún build |
| `setAwdlCurrentChannelSequenceIndex` | AUSENTE | 470 | 471 | AUSENTE en algún build |
| `isOutputFlowControlled` | 453 | 471 | 472 | slot distinto |
| `setOutputFlowControlled` | 454 | 472 | 473 | slot distinto |
| `clearOutputFlowControlled` | 455 | 473 | 474 | slot distinto |
| `flushFlowQueues` | AUSENTE | AUSENTE | 475 | AUSENTE en algún build |
| `configureAQMOutput` | 456 | 474 | 476 | slot distinto |
| `attachToBpf` | 457 | 475 | 477 | slot distinto |
| `configureIfnet` | 458 | 476 | 478 | slot distinto |
| `reset` | 459 | 477 | 479 | slot distinto |
| `dupAndTransmitMcastPacket` | 460 | 478 | 480 | slot distinto |
| `freeMulticastPacket` | 461 | 479 | 481 | slot distinto |
| `triggerAWDLMulticastTx` | 462 | 480 | 482 | slot distinto |
| `p2pMulticastTx` | 463 | 481 | 483 | slot distinto |
| `signalMulticastPackets` | AUSENTE | AUSENTE | 485 | AUSENTE en algún build |
| `sendPendingPacketsToStack` | 464 | 482 | 486 | slot distinto |
| `freePendingPackets` | 465 | 483 | 487 | slot distinto |

### IO80211AWDLProtocol, bloque puro (nombres por AppleBCMWLANProximityInterface)

| método | 23J631 | 24H232 | 25G241 | estado |
|---|---|---|---|---|
| `setMacAddress` | AUSENTE | 415 | 488 | AUSENTE en algún build |
| `getSSID` | 485 | 500 | 505 | slot distinto |
| `getCHANNEL` | 486 | 501 | 506 | slot distinto |
| `getBSSID` | 487 | 502 | 507 | slot distinto |
| `getSTATE` | 488 | 503 | 508 | slot distinto |
| `getAWDL_SYNC_PARAMS` | 489 | 504 | 509 | slot distinto |
| `getAWDL_SYNC_ENABLED` | 490 | 505 | 510 | slot distinto |
| `getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | 491 | 506 | 511 | slot distinto |
| `getAWDL_ELECTION_ID` | 492 | 507 | 512 | slot distinto |
| `getAWDL_ELECTION_METRIC` | 493 | 508 | 513 | slot distinto |
| `getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 494 | 509 | 514 | slot distinto |
| `getPEER_CACHE_MAXIMUM_SIZE` | 495 | 510 | 515 | slot distinto |
| `getAWDL_MASTER_CHANNEL` | 496 | 511 | 516 | slot distinto |
| `getAWDL_SYNC_STATE` | 497 | 512 | 517 | slot distinto |
| `getAWDL_ELECTION_RSSI_THRESHOLDS` | 498 | 513 | 518 | slot distinto |
| `getAWDL_PRESENCE_MODE` | 499 | 514 | 519 | slot distinto |
| `getAWDL_ELECTION_PARAMS` | 500 | 515 | 520 | slot distinto |
| `getAWDL_DEVICE_CAPABILITIES` | 501 | 516 | 521 | slot distinto |
| `getAWDL_SECONDARY_MASTER_CHANNEL` | 502 | 517 | 522 | slot distinto |
| `getAWDL_AF_TX_MODE` | 503 | 518 | 523 | slot distinto |
| `getAWDL_MIN_RATE` | 504 | 519 | 524 | slot distinto |
| `getAWDL_ENCRYPTION_TYPE` | 505 | 520 | 525 | slot distinto |
| `getAWDL_TXCAL_PERIOD` | 506 | 521 | 526 | slot distinto |
| `getAWDL_DEV_STATS` | 507 | 522 | 527 | slot distinto |
| `getAWDL_DFSP_CONFIG` | 508 | 523 | 528 | slot distinto |
| `getAWDL_CCA` | 509 | 524 | 529 | slot distinto |
| `getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES` | 510 | 525 | 530 | slot distinto |
| `getLLW_PARAMS` | 511 | 526 | 531 | slot distinto |
| `setSSID` | 512 | 527 | 532 | slot distinto |
| `setCHANNEL` | 513 | 528 | 533 | slot distinto |
| `setAWDL_SYNC_PARAMS` | 514 | 529 | 534 | slot distinto |
| `setAWDL_SYNC_ENABLED` | 515 | 530 | 535 | slot distinto |
| `setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS` | 516 | 531 | 536 | slot distinto |
| `setAWDL_ELECTION_ID` | 517 | 532 | 537 | slot distinto |
| `setAWDL_BSSID` | 518 | 533 | 538 | slot distinto |
| `setAWDL_ELECTION_METRIC` | 519 | 534 | 539 | slot distinto |
| `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 520 | 535 | 540 | slot distinto |
| `setAWDL_MASTER_CHANNEL` | 521 | 536 | 541 | slot distinto |
| `setAWDL_SYNC_STATE` | 522 | 537 | 542 | slot distinto |
| `setAWDL_ELECTION_RSSI_THRESHOLDS` | 523 | 538 | 543 | slot distinto |
| `setAWDL_PRESENCE_MODE` | 524 | 539 | 544 | slot distinto |
| `setPEER_CACHE_CONTROL` | 525 | 540 | 545 | slot distinto |
| `setAWDL_OOB_REQUEST` | 526 | 541 | 546 | slot distinto |
| `setAWDL_SYNC_FRAME_TEMPLATE` | 527 | 542 | 547 | slot distinto |
| `setAWDL_ELECTION_PARAMS` | 528 | 543 | 548 | slot distinto |
| `setAWDL_PEER_TRAFFIC_REGISTRATION` | 529 | 544 | 549 | slot distinto |
| `setAWDL_SECONDARY_MASTER_CHANNEL` | 530 | 545 | 550 | slot distinto |
| `setAWDL_AF_TX_MODE` | 531 | 546 | 551 | slot distinto |
| `setAWDL_MIN_RATE` | 532 | 547 | 552 | slot distinto |
| `setAWDL_ENCRYPTION_KEYS` | 533 | 548 | 553 | slot distinto |
| `setAWDL_ENCRYPTION_TYPE` | 534 | 549 | 554 | slot distinto |
| `setAWDL_OOB_AUTO_REQUEST` | 535 | 550 | 555 | slot distinto |
| `setAWDL_TXCAL_PERIOD` | 536 | 551 | 556 | slot distinto |
| `setAWDL_AWDL_ADVERTISERS` | 537 | 552 | 557 | slot distinto |
| `setAWDL_DFSP_CONFIG` | 538 | 553 | 558 | slot distinto |
| `setAWDL_DFSP_UCSA_CONFIG` | 539 | 554 | 559 | slot distinto |
| `setCOMPANION_SKYWALK_IF_CREATE` | 540 | 555 | 560 | slot distinto |
| `setAWDL_OOB_SEC_PAYLOAD_REQUEST` | 541 | 556 | 561 | slot distinto |
| `setLLW_PARAMS` | 542 | 557 | 562 | slot distinto |
| `setTX_RATE` | 543 | 558 | 563 | slot distinto |
| `setP2P_COEX_PROTECTION` | 544 | 559 | 564 | slot distinto |
| `setPSF_DWELL` | 545 | 560 | 565 | slot distinto |
| `setAWDL_CONFIG` | AUSENTE | 561 | 566 | AUSENTE en algún build |
| `setAWDL_RTG_PEER_STATS_CONFIG` | 546 | 562 | 567 | slot distinto |

## 6. Cambios de firma detectados

Leídos de los nombres demangleados de los volcados (el tipo de los parámetros forma parte del
nombre); un cambio de tipo de parámetro cambia el slot aunque el nombre corto coincida:

| método | 23J631 | 24H232 | 25G241 |
|---|---|---|---|
| `setWCL_ASSOCIATE` | `apple80211_assoc_candidates*` | `apple80211AssocCandidates*` | `apple80211AssocCandidates*` |
| `getROAM_PROFILE` / `setROAM_PROFILE` | `apple80211_roam_profile_band_data*` | `apple80211_roam_profile_all_bands*` | `apple80211_roam_profile_all_bands*` |
| `setWCL_JOIN_ABORT` | `void*` | `void*` | `apple80211_wcl_abort_join*` |
| `setLinkState` (celda de la cadena, 368 → 461) | `(IO80211LinkState, unsigned, bool, unsigned)` | igual que 23J631 | `(IO80211LinkState, unsigned, bool, unsigned, unsigned)` |
| `setMacAddress` (pure en la cadena VI/AWDL) | no existe | `ether_addr&` (slot 415) | `mloAddrArray&` (slot 468; el `ether_addr&` puro pasa al 488) |

Para la capa: en 23J631 el parámetro de `setWCL_ASSOCIATE` es otro tipo (mismo puntero, otra
estructura según nombre), y los perfiles de roaming usan `apple80211_roam_profile_band_data`; el
setter `setAWDL_CONFIG` no existe y `setWCL_JOIN_ABORT` llega tipado como `void*`.

## 7. Implicaciones para el porte

- Generación por build: header slot-exact, stubs y `KnownBuilds.inc` separados para 23J631 y 24H232,
  desde los volcados de `Docs/re/io80211_23J631/` y `Docs/re/io80211_24H232/`. Reutilizar el binario
  de uno en el otro descolocaría cada llamada virtual del bloque WCL/Controller (§5).
- En 23J631 el set de stubs de InfraProtocol debe cubrir los ~96 ioctls clásicos que Sequoia retiró,
  y no debe declarar los seis métodos inexistentes (`getPLATFORM_CONFIG`, `getLogger` del controller,
  los cinco setters WCL nuevos y `setMacAddress(mloAddrArray&)`): una redeclaración caería en un slot
  que en Sonoma pertenece a otro método.
- En 23J631 el bloque AWDL no tiene `setAWDL_CONFIG` ni `setMacAddress(ether_addr&)`
  puro: `RTLAwdInterface` pierde dos invalidaciones y las 62 celdas puras restantes se recorren −15
  respecto de 24H232 (`getSSID` 485 frente a 500).
- CoreCapture: `getLogger` no existe como virtual del controller en 23J631 (sí en 24H232/25G241,
  pure en 424) y `getFaultReporterFromDriver` es concreta en 436 en Sonoma. El arranque de pipes que
  la capa hace antes de `IO80211Controller::start` respondía al pánico de 25.6.0; en Sonoma el
  requisito de partida es distinto (?) y conviene verificar qué exige su `start` antes de portarlo.
- Slots estables en los tres builds (señal favorable, no suficiente): `start` (184), `stop` (185),
  `free` (18), `getHardwareAddress` (279), `isCommandProhibited` (394), `createWorkQueue` (395),
  `enable` (266) y `disable` (267) de la interfaz.
- RegistroInfo y bring-up: este análisis cubre vtables, tamaños y constructores; los offsets de
  structs (`RegistrationInfo`, colas, pools) y las funciones no virtuales del bring-up de 25.6.0 no
  se han contrastado en 23J631/24H232 (?).

## 8. No verificado

- Los 8 bytes de `IOSkywalkNetworkInterface` que separan el puntero de expansión del flag de byte no
  se escriben en `init` (?): quedan por identificar en otras funciones si acaso se usan.
- El orden exacto de celdas dentro del tramo 128-281 de las clases de interfaz es idéntico entre
  builds salvo los eventos listados en §4; no se ha trazado cada celda individual de ese tramo (?).
- `IOService` y su tramo del controller (celdas con destino sin símbolo en los volcados) se heredan
  del SDK y no intervienen en el porte.
- La conducta en tiempo de ejecución (carga, `start`, pánicos) de ninguno de los dos builds: solo
  hay evidencia estática.

## 9. Registro

- `Docs/re/io80211_23J631/` y `Docs/re/io80211_24H232/`: seis volcados de símbolos
  (`syms.txt` = IO80211Family; `com.apple.kernel`, `IONetworkingFamily`, `IOSkywalkFamily`,
  `corecapture`, `syms_bcm.txt` = AppleBCMWLANCoreMac), `classes.txt` (nombres de clase derivados de
  los símbolos de vtables), 15 `vt_*.txt` (cadena de 9 clases, padres SDK, tres implementaciones
  Apple y proximidad) y `KC.md5`/`KC.sha256`.
- `Docs/re/io80211_25.6.0/`: añadidos `vt_IO80211VirtualInterface.txt`,
  `vt_IO80211AWDLProtocol.txt`, `vt_AppleBCMWLANProximityInterface.txt`.
- Desensamblados de constructores e `init` citados en §3: copias de las bases de análisis en
  directorio de trabajo propio, no sobre los originales.
- sha256 de los KC: 23J631 `3f5dcde57f2d0ce27b9c9abf3d1e3d2c36b33cea9ae0a7eb36bf9c4282ed8cd2`,
  24H232 `ddd9016e225a61ebfd4f04b02a00937262f26690c57a928613e08506978c81be`,
  25G241 `21d6da1c4c3e7b72dddf5144da8a4c4b48e294249a16018a5006b61bb2155ccc`.
