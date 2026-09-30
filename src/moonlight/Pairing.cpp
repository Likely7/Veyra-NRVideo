// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/moonlight/Pairing.h"

#include <cstdio>

namespace veyra::moonlight {

namespace {

constexpr int kRequestTimeoutMs = 5000;
// What Sunshine shows in its list of paired clients.
constexpr const char* kDeviceName = "Veyra";

std::string tag(const xml::Node& root, const char* name) { return xml::textOf(root, name); }

// The steps after the first check "paired == 1"; anything else aborts the pairing.
bool pairedFlag(const xml::Node& root) { return tag(root, "paired") == "1"; }

} // namespace

std::string generatePin() {
    const std::string random = crypto::randomBytes(4);
    uint32_t value = 0;
    for (unsigned char c : random) value = (value << 8) | c;
    char text[8];
    std::snprintf(text, sizeof(text), "%04u", value % 10000u);
    return text;
}

PairOutcome pairWithHost(const ServerClient& original, const Identity& identity, const ServerInfo& host,
                         const std::string& pin, const std::atomic<bool>* cancel) {
    ServerClient client(original);
    client.setCancel(cancel);
    PairOutcome outcome;
    const auto fail = [&](const char* detail) {
        outcome.result = PairResult::Failed;
        outcome.detail = detail;
        return outcome;
    };
    // Best effort: tell the host to forget a half finished pairing. It must not be
    // cancelled by the same flag, and failing is fine.
    const auto unpair = [&]() {
        try {
            ServerClient cleanup(original);
            cleanup.request(false, "unpair", "", 2000);
        } catch (...) {}
    };

    const bool sha256 = host.appVersionMajor() >= 7;   // generation 7 and later hash with SHA-256
    const size_t hashLength = sha256 ? 32 : 20;
    const std::string common = std::string("devicename=") + kDeviceName + "&updateState=1&";

    try {
        const std::string salt = crypto::randomBytes(16);
        std::string aesKey = crypto::digest(salt + pin, sha256);
        aesKey.resize(16);

        // Stage 1: send our certificate, get the host's. This blocks until the PIN is entered.
        const xml::Node getCert = ServerClient::checkedRoot(client.request(false, "pair",
            common + "phrase=getservercert&salt=" + xml::toHex(salt) + "&clientcert=" + xml::toHex(identity.certPem), 0));
        if (!pairedFlag(getCert)) return fail("stage 1: the host refused the pairing request");
        const auto serverCertBytes = xml::fromHex(tag(getCert, "plaincert"));
        if (!serverCertBytes || serverCertBytes->empty()) {
            // The host is already pairing another client.
            unpair();
            outcome.result = PairResult::AlreadyInProgress;
            outcome.detail = "the host is already pairing";
            return outcome;
        }
        const std::string serverCert = *serverCertBytes;
        if (crypto::certificateDer(serverCert).empty()) { unpair(); return fail("stage 1: unreadable server certificate"); }

        // Stage 2: the host proves it knows the PIN.
        const std::string randomChallenge = crypto::randomBytes(16);
        const xml::Node challenge = ServerClient::checkedRoot(client.request(false, "pair",
            common + "clientchallenge=" + xml::toHex(crypto::aesEcbEncrypt(randomChallenge, aesKey)), kRequestTimeoutMs));
        if (!pairedFlag(challenge)) { unpair(); return fail("stage 2: challenge rejected"); }
        const auto responseBytes = xml::fromHex(tag(challenge, "challengeresponse"));
        const std::string challengeResponse = responseBytes ? crypto::aesEcbDecrypt(*responseBytes, aesKey) : std::string();
        if (challengeResponse.size() < hashLength + 16) { unpair(); return fail("stage 2: challenge response too short"); }

        // Stage 3: answer the host's challenge with a hash over it, our certificate signature and a secret.
        const std::string clientSecret = crypto::randomBytes(16);
        const std::string serverResponse = challengeResponse.substr(0, hashLength);
        std::string mix = challengeResponse.substr(hashLength, 16);
        mix += crypto::certificateSignature(identity.certPem);
        mix += clientSecret;
        std::string padded = crypto::digest(mix, sha256);
        padded.resize(32);
        const xml::Node secretStage = ServerClient::checkedRoot(client.request(false, "pair",
            common + "serverchallengeresp=" + xml::toHex(crypto::aesEcbEncrypt(padded, aesKey)), kRequestTimeoutMs));
        if (!pairedFlag(secretStage)) { unpair(); return fail("stage 3: response rejected"); }
        const auto pairingSecret = xml::fromHex(tag(secretStage, "pairingsecret"));
        if (!pairingSecret || pairingSecret->size() <= 16) { unpair(); return fail("stage 3: pairing secret too short"); }

        // The host signed its secret with the key of the certificate it gave us: a
        // mismatch means somebody else answered (man in the middle).
        const std::string serverSecret = pairingSecret->substr(0, 16);
        if (!crypto::verify(serverSecret, pairingSecret->substr(16), serverCert)) { unpair(); return fail("signature check failed: possible man in the middle"); }

        // The host's hash over our challenge proves it used the same PIN.
        std::string expected = randomChallenge;
        expected += crypto::certificateSignature(serverCert);
        expected += serverSecret;
        if (crypto::digest(expected, sha256) != serverResponse) {
            unpair();
            outcome.result = PairResult::PinWrong;
            outcome.detail = "the PIN does not match";
            return outcome;
        }

        // Stage 4: our own secret, signed, so the host can check us the same way.
        const xml::Node clientSecretStage = ServerClient::checkedRoot(client.request(false, "pair",
            common + "clientpairingsecret=" + xml::toHex(clientSecret + crypto::sign(identity, clientSecret)), kRequestTimeoutMs));
        if (!pairedFlag(clientSecretStage)) { unpair(); return fail("stage 4: pairing secret rejected"); }

        // Stage 5: prove the new pairing works over HTTPS with the pinned certificate.
        client.setPinnedServerCertificate(serverCert);
        const xml::Node finalStage = ServerClient::checkedRoot(client.request(true, "pair", common + "phrase=pairchallenge", kRequestTimeoutMs));
        if (!pairedFlag(finalStage)) { unpair(); return fail("stage 5: HTTPS check failed"); }

        outcome.result = PairResult::Paired;
        outcome.serverCertPem = serverCert;
        return outcome;
    } catch (const TransportError& e) {
        if (e.kind == TransportError::Kind::Cancelled) {
            unpair();
            outcome.result = PairResult::Cancelled;
            outcome.detail = "cancelled";
            return outcome;
        }
        unpair();
        return fail(e.what());
    } catch (const StatusError& e) {
        unpair();
        return fail(e.what());
    }
}

} // namespace veyra::moonlight
