// Unwrap an AEA image key: hpke_open <fcs key.pem> <fcs-response.json>  -> base64 key on stdout
import Foundation
import CryptoKit
let pem = try String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8)
let key = try P256.KeyAgreement.PrivateKey(pemRepresentation: pem)
let j = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[2]))) as! [String: String]
let enc = Data(base64Encoded: j["enc-request"]!)!
let wrapped = Data(base64Encoded: j["wrapped-key"]!)!
var r = try HPKE.Recipient(privateKey: key, ciphersuite: .P256_SHA256_AES_GCM_256, info: Data(), encapsulatedKey: enc)
let k = try r.open(wrapped)
print(k.base64EncodedString())
