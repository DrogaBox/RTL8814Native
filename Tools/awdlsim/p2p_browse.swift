// Keeps awdl0 in use without AirDrop: a Bonjour browse with peer-to-peer
// enabled makes mDNSResponder ask wifip2pd for AWDL, as the AirDrop window
// does. Needed while there is no Bluetooth controller, when the AirDrop
// setting stays at No One.
//
//   swift Tools/awdlsim/p2p_browse.swift [seconds] [service]   (default 120, _airdrop._tcp)
//
// Copyright (c) 2026 DrogaBox - GPL-2.0
import Foundation
import Network

let args = CommandLine.arguments
let seconds = args.count > 1 ? Double(args[1]) ?? 120 : 120
let service = args.count > 2 ? args[2] : "_airdrop._tcp"

let params = NWParameters()
params.includePeerToPeer = true
let browser = NWBrowser(for: .bonjour(type: service, domain: "local."), using: params)
browser.stateUpdateHandler = { print("[browse] state \($0)") }
browser.browseResultsChangedHandler = { results, _ in
    for r in results { print("[browse] \(r.endpoint) \(r.interfaces.map { $0.name })") }
}
browser.start(queue: .main)
print("[browse] \(service) with peer-to-peer for \(Int(seconds)) s; ifconfig awdl0 should say status: active")
DispatchQueue.main.asyncAfter(deadline: .now() + seconds) {
    browser.cancel()
    exit(0)
}
dispatchMain()
