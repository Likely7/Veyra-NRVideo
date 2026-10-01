// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/moonlight/Crypto.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <cctype>
#include <memory>
#include <new>
#include <stdexcept>

namespace veyra::moonlight {

namespace {

struct BioFree { void operator()(BIO* p) const { BIO_free_all(p); } };
struct X509Free { void operator()(X509* p) const { X509_free(p); } };
struct PkeyFree { void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); } };
struct CtxFree { void operator()(EVP_CIPHER_CTX* p) const { EVP_CIPHER_CTX_free(p); } };
struct MdFree { void operator()(EVP_MD_CTX* p) const { EVP_MD_CTX_free(p); } };

using BioPtr = std::unique_ptr<BIO, BioFree>;
using X509Ptr = std::unique_ptr<X509, X509Free>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyFree>;

X509Ptr readCertificate(std::string_view pem) {
    if (pem.empty() || pem.size() > (1u << 16)) return nullptr;
    BioPtr bio(BIO_new_mem_buf(pem.data(), int(pem.size())));
    if (!bio) return nullptr;
    return X509Ptr(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

PkeyPtr readPrivateKey(std::string_view pem) {
    if (pem.empty() || pem.size() > (1u << 16)) return nullptr;
    BioPtr bio(BIO_new_mem_buf(pem.data(), int(pem.size())));
    if (!bio) return nullptr;
    return PkeyPtr(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

std::string memoryContents(BIO* bio) {
    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);
    return mem ? std::string(mem->data, mem->length) : std::string();
}

bool isLowerHex16(const std::string& id) {
    if (id.size() != 16) return false;
    for (char c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::string aesEcb(std::string_view input, std::string_view key, bool encrypt) {
    if (input.empty() || input.size() % 16 || key.size() < 16) return {};
    std::unique_ptr<EVP_CIPHER_CTX, CtxFree> ctx(EVP_CIPHER_CTX_new());
    if (!ctx) throw std::bad_alloc();
    if (EVP_CipherInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, reinterpret_cast<const unsigned char*>(key.data()), nullptr, encrypt ? 1 : 0) != 1) return {};
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);
    std::string out(input.size(), '\0');
    int produced = 0;
    if (EVP_CipherUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &produced,
                         reinterpret_cast<const unsigned char*>(input.data()), int(input.size())) != 1 || size_t(produced) != input.size()) return {};
    return out;
}

} // namespace

bool Identity::valid() const {
    if (!isLowerHex16(uniqueId)) return false;
    auto cert = readCertificate(certPem);
    auto key = readPrivateKey(keyPem);
    if (!cert || !key) return false;
    // The key must be the one the certificate was issued for.
    return X509_check_private_key(cert.get(), key.get()) == 1;
}

Identity createIdentity() {
    PkeyPtr key(EVP_RSA_gen(2048));
    if (!key) throw std::runtime_error("RSA key generation failed");
    X509Ptr cert(X509_new());
    if (!cert) throw std::bad_alloc();
    X509_set_version(cert.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 0);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), 60L * 60 * 24 * 365 * 20);
    X509_set_pubkey(cert.get(), key.get());
    X509_NAME* name = X509_NAME_new();
    if (!name) throw std::bad_alloc();
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("NVIDIA GameStream Client"), -1, -1, 0);
    X509_set_subject_name(cert.get(), name);
    X509_set_issuer_name(cert.get(), name);
    X509_NAME_free(name);
    if (X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0) throw std::runtime_error("certificate signing failed");

    BioPtr keyBio(BIO_new(BIO_s_mem())), certBio(BIO_new(BIO_s_mem()));
    if (!keyBio || !certBio) throw std::bad_alloc();
    PEM_write_bio_PrivateKey(keyBio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr);
    PEM_write_bio_X509(certBio.get(), cert.get());

    Identity identity;
    identity.keyPem = memoryContents(keyBio.get());
    identity.certPem = memoryContents(certBio.get());
    static constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : crypto::randomBytes(8)) { identity.uniqueId.push_back(digits[c >> 4]); identity.uniqueId.push_back(digits[c & 15]); }
    if (!identity.valid()) throw std::runtime_error("newly generated identity is unreadable");
    return identity;
}

namespace crypto {

std::string randomBytes(size_t count) {
    std::string out(count, '\0');
    if (count && RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), int(count)) != 1) throw std::runtime_error("RAND_bytes failed");
    return out;
}

std::string digest(std::string_view data, bool sha256) {
    unsigned char buffer[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(data.data(), data.size(), buffer, &length, sha256 ? EVP_sha256() : EVP_sha1(), nullptr) != 1) return {};
    return std::string(reinterpret_cast<char*>(buffer), length);
}

std::string aesEcbEncrypt(std::string_view plain, std::string_view key) { return aesEcb(plain, key, true); }
std::string aesEcbDecrypt(std::string_view cipher, std::string_view key) { return aesEcb(cipher, key, false); }

std::string sign(const Identity& identity, std::string_view message) {
    auto key = readPrivateKey(identity.keyPem);
    if (!key) return {};
    std::unique_ptr<EVP_MD_CTX, MdFree> ctx(EVP_MD_CTX_new());
    if (!ctx) throw std::bad_alloc();
    if (EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1) return {};
    if (EVP_DigestSignUpdate(ctx.get(), message.data(), message.size()) != 1) return {};
    size_t length = 0;
    if (EVP_DigestSignFinal(ctx.get(), nullptr, &length) != 1) return {};
    std::string signature(length, '\0');
    if (EVP_DigestSignFinal(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()), &length) != 1) return {};
    signature.resize(length);
    return signature;
}

bool verify(std::string_view message, std::string_view signature, std::string_view certPem) {
    auto cert = readCertificate(certPem);
    if (!cert) return false;
    PkeyPtr key(X509_get_pubkey(cert.get()));
    if (!key) return false;
    std::unique_ptr<EVP_MD_CTX, MdFree> ctx(EVP_MD_CTX_new());
    if (!ctx) throw std::bad_alloc();
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1) return false;
    if (EVP_DigestVerifyUpdate(ctx.get(), message.data(), message.size()) != 1) return false;
    return EVP_DigestVerifyFinal(ctx.get(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size()) == 1;
}

std::string certificateSignature(std::string_view certPem) {
    auto cert = readCertificate(certPem);
    if (!cert) return {};
    const ASN1_BIT_STRING* signature = nullptr;
    X509_get0_signature(&signature, nullptr, cert.get());
    if (!signature) return {};
    return std::string(reinterpret_cast<const char*>(ASN1_STRING_get0_data(signature)), size_t(ASN1_STRING_length(signature)));
}

std::string certificateDer(std::string_view certPem) {
    auto cert = readCertificate(certPem);
    if (!cert) return {};
    unsigned char* der = nullptr;
    const int length = i2d_X509(cert.get(), &der);
    if (length <= 0 || !der) return {};
    std::string out(reinterpret_cast<char*>(der), size_t(length));
    OPENSSL_free(der);
    return out;
}

} // namespace crypto
} // namespace veyra::moonlight
