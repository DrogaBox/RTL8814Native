# Modo datos del manager: por qué la secuencia no se ensanchaba

Medido y leído en macOS 26.7.1 (25G241), T9UH y el par virtual, el 2026-11-06. Las direcciones son del IDB de 25G241.

## 1. Lo medido

Durante las dos transferencias del [AWDL_PAR_VIRTUAL.md](AWDL_PAR_VIRTUAL.md) §8.1, 1 MiB hacia el Mac y 4 MiB desde el Mac, el registro del kernel
no cambió de estado. Con en2 en el 149, `AWDL ON` dice:

| Estado | Secuencia | Canal en cuántas franjas de 16 |
|---|---|---|
| `Low Power`, durante toda la transferencia | `149 0 149 0 0 0 0 0 6 0 149 0 0 0 0 0` | 3 en el 149 |
| `Idle` | `149 149 149 0 0 0 0 0 6 149 149 0 0 0 0 0` | 5 en el 149 |
| `Discovery` | `149 149 149 149 0 0 0 0 6 149 149 149 0 0 0 0` | 7 en el 149 |
| `Data + Infra Idle`, solo unos instantes del arranque | `149 149 149 149 149 149 149 149 6 149 149 149 149 149 149 149` | 15 en el 149 |

Con 3 franjas de 16, la subida queda en 113 KB/s. El iPhone se ensancha con tráfico (§15 de [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md)), y el
Mac no lo hacía.

## 2. Qué decide el modo datos

`IO80211AWDLPeerManager::monitorAWDLDataMode` (`0xffffff80021e35aa`) corre desde `reportDataPathEvents`, en el evento de inicio de ventana, cada
tantas franjas como diga `ivars+16064`. Compara con la pasada anterior dos contadores de la base, en `*(pm+0x18)`:

| Campo | Contenido | Quién lo incrementa |
|---|---|---|
| `+1968` | Paquetes unicast de TX | `skywalkLogTxPacket`, desde `logSkywalkTxReqPacket` |
| `+1948` | Paquetes unicast de RX | `skywalkInputPacket` |

Si hubo TX y TX más RX llega a `ivars+16060`, llama a `setDataMode(6, 0)`, cuyo motivo es `RptDataPathThres`. `setDataMode` pasa al estado 5, `Data`,
o a otro de datos si hay un escaneo en curso. `initParamsToDefaults` deja `ivars+16060` en 8, `ivars+16064` en 8 y `ivars+16068` en 4. Sin
tráfico durante `16 × presencia × (ivars+16064 + ivars+16072)` ms, vuelve a `Idle` y más tarde a `Low Power`.

## 3. La causa

La capa no pasa por `logSkywalkTxReqPacket` ni por `skywalkInputPacket`: `txData` solo llama a `preQueuePacketCheckQuietMode`
([AWDL_TX_HOOKS_POR_PAQUETE.md](AWDL_TX_HOOKS_POR_PAQUETE.md) §2) y `deliverData` entrega directamente a la cola de RX. Los contadores siguen en 0,
así que para el manager nunca hay tráfico.

`preQueuePacketCheckQuietMode` llama a `setDataMode(3, 0)` solo si el manager está en modo silencioso, de modo que esa vía tampoco basta.

## 4. El cambio

`txData` y `deliverData` incrementan los contadores de paquetes y de bytes (`+1968` y `+1988`, `+1948` y `+1992`) para cada paquete unicast. Son
desplazamientos fijos de 25G241, y otras versiones del sistema necesitarán los suyos.

Los demás lectores de esos campos no tienen efecto sobre el camino de datos: `gatherInfraStats`, `updateServiceRegistrationStats`,
`updatePacketStatsPerMonitor`, y los registros de `interfaceMonitor`, `printPackets` y `printDataPath`. Ninguno los compara con las finalizaciones de
TX, así que no hay un detector de atascos que dispare por contarlos sin pasar por el resto del camino.

`logSkywalkTxReqPacket` entera queda sin llamar, porque exige el `PacketSkywalkScratch` del paquete, y la capa no lo maneja. No se actualizan
`peer_ivars+184`, el recuento por AC ni el tap BPF de esa función.

En 24H232 (15.8.2) los mismos campos están 16 bytes antes, leídos en IDA y sin ejecutar: `+1952` y `+1972` de TX, `+1932` y `+1976` de RX, con las cadenas `current Tx count` y `current Rx count` en `monitorAWDLDataMode` (`0xffffff80021801ce`).

## 5. Lo medido con el cambio

Capa `43ACC5D0`, en2 en el 149, 2026-11-06.

| Prueba | Antes | Con el cambio |
|---|---|---|
| 4 MiB de TCP desde el Mac al par virtual | 113 KB/s | 507 KB/s de media, 8,1 s, archivo idéntico, MSS 1356 |
| Estado durante esa transferencia | `Low Power` entero | `Data + Infra Idle` a unos 1,3 s, con `RptDataPathThres` |
| 4 MiB por AirDrop hacia el Mac | sin medir con ese tamaño | `/Upload -> 200` en unos 2 s, archivo idéntico |
| Unicast al router, 50 tramas/s | 494 tramas con ACK a continuación, una copia cada una, sin reintentos | 495 tramas de 494 números de secuencia, 494 ACK, 1 reintento |

Durante la prueba con el router, el Mac estuvo en `Data + Infra Idle` de principio a fin. Al terminar pasa a `Idle` en unos 2 s, y a `Low Power` unos 5 s
después por `IdleTimeout`. Los pasos con `RptDataPathThres` salen cada 1,05 s, es decir, cada 16 franjas. Falta revisar cómo encaja eso con `ivars+16064` `?`.

La ruta hacia el Mac no pasa por el aire con el par virtual, así que esa fila solo comprueba que el recuento de RX no estorba.

El anillo de TX acumuló 6 descartes por anillo lleno tras el TCP desde el Mac y 567 tras el AirDrop y la prueba con el router, que va a 50 tramas/s. Por
eliminación, esos 561 son ACK de TCP del AirDrop, cuya subida ocurre casi toda en `Idle`, con huecos de varias franjas sin ticks. El anillo tira la trama más
vieja y el ACK de TCP es acumulativo, de modo que el archivo llegó igual. No se contó por separado `?`.

## 6. El límite que queda

La velocidad ahora la fija el presupuesto de `sendData`: 12 ms de aire por tick a 12 Mb/s. Una trama de 1468 bytes cuesta 1138 µs, así que caben 10 por tick.
Cada franja servida tiene 3 ticks, porque la última AW no envía datos.

| Estado | Ticks en 16 franjas (1,05 s) | Tope de TCP con MSS 1356 | Medido |
|---|---|---|---|
| `Low Power`, 3 franjas | 9 | 114 KB/s | 113 KB/s |
| `Data`, 15 franjas | 45 | 568 KB/s | unos 580 KB/s, calculados sin los primeros 1,3 s |

La tasa de 12 Mb/s y el presupuesto no cambian con este arreglo. Subir de ahí exige otra tasa de datos en la radio.

## 7. Posición de los datos en modo datos

Las tramas salen solo en los ticks. Plegadas sobre el período de 64 TU de la captura del router, forman tres grupos, separados 13,5, 16,5 y 34 TU. Eso
coincide con el final de la guarda en el TU 2,5 y con los ticks de los TU 16 y 32, seguidos de la AW sin datos. La guarda del §16 de
[AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md) sigue actuando, y solo en la primera AW de cada franja.

`slotfine.py` da aquí 244 de 494 antes del TU 3, porque calcula la posición a partir del PSF y un PSF retrasado la adelanta. Las tramas del TU 16 y las del final de la
guarda salen en ese recuento como TU 13-15 y 1-2, con el mismo desfase de unos 1,5 TU. Para comparar posiciones en este estado sirve el plegado, no el valor absoluto.

## 8. Sin medir

- Con un iPhone real: si el iPhone y el Mac coinciden en más franjas, y la velocidad por el aire.
- Con la red en un canal que no sea social: una secuencia más ancha aumenta el tiempo fuera de ese canal `?`.

## 9. Datos a un par que no está en la franja

Coste en la BSS con en2 y `awdl0` en el 149, medido con un ping cada 0,2 s al router por en2 y el A53 en monitor:

| Tráfico por `awdl0` | Tramas de datos al aire | Copias por trama | Ping por en2 |
|---|---|---|---|
| Ninguno | 0 | - | 0 % de pérdida, 1,9 ms de media |
| UDP al router, que responde con ACK | 430/s | 1 | 0 % de pérdida, 8,7 ms de media, 336 ms de máximo |
| TCP al par virtual, sin ACK | 808/s | 2 | Ninguna trama de en2 sale durante la transferencia; RTT de hasta 6,7 s |

Las 430 tramas/s coinciden con el techo del §6, y con ACK la red sigue funcionando. El par virtual es otro caso: el límite de 1 reintento de `AWDLTx.c` da 2 copias, el
presupuesto cuenta una y la cola de gestión queda llena sin pausa. Por eso no sirve para medir el coste en la BSS.

Una trama sin ACK con un par real gasta las 7 copias del límite normal, más que en esa fila. En reposo el iPhone comparte con el Mac solo las franjas 0, 2 y 10
(§15 de [AWDL_A53_SNIFFER.md](AWDL_A53_SNIFFER.md)). Hasta este arreglo, las 3 franjas del Mac eran justo esas. Con el Mac en `Data` y el iPhone todavía en reposo, o con el
iPhone en otra red, 12 de las 15 franjas enviarían a nadie, y crosswind ya evita ese caso (AWDL-16).

La capa ahora guarda el TLV 18 que anuncia cada par (`notePeerSeq`). `sendData` deja la trama al frente del anillo si su destino anunció otro canal para esa franja, y un
destino de grupo pregunta por todos los pares. Ante la duda, envía: sin nada reciente del par en 3 s, si su número de AW no cae en la franja del Mac, sin franjas suyas en el
canal o sin franjas comunes a todos. La línea periódica del registro suma `held N`.

Medido con la capa `07BC6107`, 4 MiB de TCP hacia el par virtual. `awdlsim --rest` hace que el nodo anuncie solo las franjas 0, 2 y 10; el Mac anuncia las 15 de `Data` en las dos filas:

| Nodo | TCP | Primeras copias por franja | Ping por en2 |
|---|---|---|---|
| Con `--rest` | 113 KB/s y 112 KB/s, idéntico | 0: 357, 2: 341, 10: 336, 11: 4; ninguna en las demás | 0 % de pérdida, 6,7 ms de media, 71 ms de máximo |
| Sin `--rest` | 542 KB/s, idéntico | Entre 137 y 225 en las franjas 0-7 y 9-15, 78 en la 8 | Sin pérdida, pero hasta 7 s de retraso, por las 2 copias de arriba |

Con `--rest`, el coste en la BSS deja de ser el de la tercera fila de la primera tabla aunque el nodo siga sin dar ACK. Las 78 tramas de la franja 8 y las 4 de la 11 quedan fuera de lo
que el Mac o el nodo anuncian, y se atribuyen, sin haberlo comprobado, a la cola llena que se vacía tarde o al desfase del §7. Con UDP al router, con esta capa: 430 tramas/s, 0 % de
pérdida, 3,7 ms de media y 16 ms de máximo.

La línea del registro corta en 192 caracteres, y dejaba `held` fuera. La capa `2E3D2058` lo mueve junto a los contadores de datos, y está cargada: sin par da `held 0`. El número de veces que
retuvo solo se ve en el reparto por franjas de arriba, y con un par real sigue sin medir.

El presupuesto sigue sin contar reintentos, así que un par presente que pierde muchas tramas aún puede llenar el aire `?`.
