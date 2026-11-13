# Capacidad 78 («Concurrency supported»): SoftAP más AWDL, no infraestructura

`AWDL_CARD_CAPS_RESTANTES.md` dejó abierta la capacidad 78 (`p[13]` bit `0x40`, byte del kernel
`+0xB3D`): `IO80211AWDLPeerManager::configure` la lee en `0xffffff80021c58b3`, registra
«Concurrency supported» y pone a 1 el byte `state+0xD066` (escritura en `0xffffff80021c58ec`;
`initParamsToDefaults` lo vuelve a 0 en `0xffffff80021c7f45`). La pregunta era si esa
concurrencia es la de AWDL con una asociación de infraestructura, y por tanto si la capa debería
anunciarla para el modo `rtlawdl_mcc`. Análisis estático del kernel collection de 25G241.

## 1. Lectores de `state+0xD066`

Sólo dos funciones lo leen, las dos sobre el punto de acceso por software (SoftAP / hotspot):

- `IO80211AWDLPeerManager::validateForHostapAWDLConcurrency` (`0xffffff80021d8a54`, prueba en
  `0xffffff80021d8a80`). La llaman `enableAWDL` (`0xffffff80021dd37d`), `exitQuietMode`
  (`0xffffff80021fae5b`), `setSoftApCSANanPh` (`0xffffff8002219f95`) e
  `IO80211P2PSupervisor::requestNANEnable` (`0xffffff8002391a1c`). Con el bit puesto y un SoftAP
  activo (`state+0xD06C` distinto de 0), si el canal del SoftAP (`state+0xD070`) no es el social
  elegido (el de 5 GHz configurado, si no 149, si no 44, si no el canal maestro), ordena un cambio
  de canal del SoftAP con `hostapForceChannelSwitch(canal, 2)` y registra «Sending CSA for channel
  %u». Si ese CSA falla, devuelve 0 con «CSA on SoftAP failed. Failure to enable AWDL». Con el bit
  claro se salta toda esa rama, conserva el canal del SoftAP y devuelve 1.
- `IO80211AWDLPeerManager::notifyHostapState` (`0xffffff80021fec60`, prueba en
  `0xffffff80021fec6e`; sin llamadores directos, entra por la vtable): con el bit puesto copia el
  estado y el canal del SoftAP en `state+0xD06C/0xD070/0xD072`, apaga AWDL en una migración si ya
  estaba encendido y llama `setHostApAWDLConcurrency(3 u 8)`.

Ninguno toca la interfaz de infraestructura, la secuencia de canales con el canal infra, las
ventanas de disponibilidad ni la elección. La relación entre AWDL y la asociación del cliente
sale de `getInfrastructureState` (`AWDL_SECUENCIA_CON_INFRA.md`), no de esta capacidad.

## 2. Veredicto para la capa RTL

La capacidad 78 describe a una radio que puede mantener un SoftAP y AWDL a la vez, moviendo el
SoftAP al canal social con un CSA. La 8814AU con este driver no ofrece SoftAP a IO80211 ni
implementa el CSA del SoftAP, así que anunciarla sólo abriría la ruta que acaba en «Failure to
enable AWDL» si algún día hubiera un SoftAP. Para el tiempo compartido con la red del router
(`rtlawdl_mcc`) no aporta nada: debe quedar clara, como hoy.

Siguen abiertos `state+0x103A0` y `state+0x102F1` (bits 2 y 3 de `p[24]`) y `sub+0x6FC` (código de
país dinámico) `?`.
