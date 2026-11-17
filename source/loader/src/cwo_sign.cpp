// CwoSign.exe - the BFR pack's update signing tool (owner 2026-11-12). Independently authored from official Windows documentation only: CNG (bcrypt.h) -
// BCryptGenerateKeyPair / BCryptExportKey / BCryptImportKeyPair with BCRYPT_ECDSA_P256_ALGORITHM, BCryptHash (SHA-256),
// BCryptSignHash / BCryptVerifySignature (64-byte r || s signatures).
//   CwoSign keygen  PRIVATE.key PUBLIC.h     a new key pair: the private blob (keep it on the owner's PC, never in a repository)
//                                            and the public blob as a C header for the launcher (cwo_update_key.h)
//   CwoSign sign    PRIVATE.key FILE OUT.sig the signature of FILE's SHA-256 as 128 hex digits
//   CwoSign verify  PUBLIC.h|PRIVATE.key FILE SIG   exit 0 when the signature is good
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#pragma comment(lib, "bcrypt.lib")

static std::vector<unsigned char> read_all(const wchar_t *p) {
    std::vector<unsigned char> b;
    FILE *f = NULL;
    if (_wfopen_s(&f, p, L"rb") || !f) return b;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0) { b.resize(n); if (fread(b.data(), 1, n, f) != (size_t)n) b.clear(); }
    fclose(f);
    return b;
}
static bool write_all(const wchar_t *p, const void *d, size_t n) {
    FILE *f = NULL;
    if (_wfopen_s(&f, p, L"wb") || !f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    fclose(f);
    return ok;
}
static bool sha256(const unsigned char *d, size_t n, unsigned char out[32]) {
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)d, (ULONG)n, out, 32) == 0;
}
static std::vector<unsigned char> public_from_header(const std::vector<unsigned char> &h) {   // "0x12, 0x34, ..."
    std::vector<unsigned char> b;
    std::string s(h.begin(), h.end());
    size_t i = s.find('{');
    while (i != std::string::npos && (i = s.find("0x", i)) != std::string::npos) { b.push_back((unsigned char)strtoul(s.c_str() + i, NULL, 16)); i += 2; }
    return b;
}

int wmain(int argc, wchar_t **argv) {
    BCRYPT_ALG_HANDLE alg = NULL;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0)) { wprintf(L"no ECDSA P-256 provider\n"); return 2; }
    if (argc == 4 && !_wcsicmp(argv[1], L"keygen")) {
        if (GetFileAttributesW(argv[2]) != INVALID_FILE_ATTRIBUTES) { wprintf(L"refused: %s exists (a new key would orphan every launcher)\n", argv[2]); return 3; }
        BCRYPT_KEY_HANDLE k = NULL;
        if (BCryptGenerateKeyPair(alg, &k, 256, 0) || BCryptFinalizeKeyPair(k, 0)) { wprintf(L"keygen failed\n"); return 4; }
        ULONG n = 0;
        unsigned char priv[512], pub[256];
        if (BCryptExportKey(k, NULL, BCRYPT_ECCPRIVATE_BLOB, priv, sizeof(priv), &n, 0)) return 5;
        if (!write_all(argv[2], priv, n)) return 6;
        if (BCryptExportKey(k, NULL, BCRYPT_ECCPUBLIC_BLOB, pub, sizeof(pub), &n, 0)) return 7;
        FILE *f = NULL;
        if (_wfopen_s(&f, argv[3], L"w") || !f) return 8;
        fprintf(f, "// the BFR pack's update public key (BCRYPT_ECCPUBLIC_BLOB, ECDSA P-256) - made by CwoSign keygen\n"
                   "static const unsigned char CWO_UPDATE_PUBKEY[%lu] = {", n);
        for (ULONG i = 0; i < n; i++) fprintf(f, "%s0x%02x", i ? (i % 16 ? ", " : ",\n    ") : "\n    ", pub[i]);
        fprintf(f, "};\n");
        fclose(f);
        BCryptDestroyKey(k);
        wprintf(L"key pair made: private %s, public %s\n", argv[2], argv[3]);
        return 0;
    }
    if (argc == 5 && !_wcsicmp(argv[1], L"sign")) {
        std::vector<unsigned char> priv = read_all(argv[2]), data = read_all(argv[3]);
        if (priv.empty() || data.empty()) { wprintf(L"cannot read the key or the file\n"); return 9; }
        BCRYPT_KEY_HANDLE k = NULL;
        if (BCryptImportKeyPair(alg, NULL, BCRYPT_ECCPRIVATE_BLOB, &k, priv.data(), (ULONG)priv.size(), 0)) { wprintf(L"bad private key\n"); return 10; }
        unsigned char h[32], sig[64];
        ULONG n = 0;
        if (!sha256(data.data(), data.size(), h) || BCryptSignHash(k, NULL, h, 32, sig, sizeof(sig), &n, 0) || n != 64) { wprintf(L"sign failed\n"); return 11; }
        char hex[130];
        for (int i = 0; i < 64; i++) sprintf_s(hex + 2 * i, 3, "%02x", sig[i]);
        hex[128] = '\n'; hex[129] = 0;
        if (!write_all(argv[4], hex, 129)) return 12;
        if (BCryptVerifySignature(k, NULL, h, 32, sig, 64, 0)) { wprintf(L"self-check failed\n"); return 13; }
        wprintf(L"signed %s -> %s\n", argv[3], argv[4]);
        return 0;
    }
    if (argc == 5 && !_wcsicmp(argv[1], L"verify")) {
        std::vector<unsigned char> kb = read_all(argv[2]), data = read_all(argv[3]), sh = read_all(argv[4]);
        bool header = kb.size() > 0 && kb[0] == '/';
        if (header) kb = public_from_header(kb);
        BCRYPT_KEY_HANDLE k = NULL;
        if (kb.empty() || BCryptImportKeyPair(alg, NULL, header ? BCRYPT_ECCPUBLIC_BLOB : BCRYPT_ECCPRIVATE_BLOB, &k, kb.data(), (ULONG)kb.size(), 0)) { wprintf(L"bad key\n"); return 14; }
        unsigned char h[32], sig[64];
        if (sh.size() < 128 || data.empty() || !sha256(data.data(), data.size(), h)) { wprintf(L"bad input\n"); return 15; }
        for (int i = 0; i < 64; i++) { char t[3] = {(char)sh[2 * i], (char)sh[2 * i + 1], 0}; sig[i] = (unsigned char)strtoul(t, NULL, 16); }
        bool ok = BCryptVerifySignature(k, NULL, h, 32, sig, 64, 0) == 0;
        wprintf(L"%s\n", ok ? L"signature GOOD" : L"signature BAD");
        return ok ? 0 : 1;
    }
    wprintf(L"usage: CwoSign keygen PRIVATE.key PUBLIC.h | sign PRIVATE.key FILE OUT.sig | verify PUBLIC.h|PRIVATE.key FILE SIG\n");
    return 2;
}
