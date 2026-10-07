/* wShell key management: direct library calls, no helper executable or service. */
#include "putty.h"
#include "ssh.h"
#include "sshkeygen.h"
#include "mpint.h"
#include "keys.h"
#include <sddl.h>
#include <bcrypt.h>

struct WsKey { ssh2_userkey *key; };
static char keyError[256];
static void fail(const char *error) { snprintf(keyError, sizeof(keyError), "%s", error ? error : "Key operation failed."); }
const char *wsKeyError(void) { return keyError; }
static WsKey *wrap(ssh2_userkey *key) { WsKey *out = snew(WsKey); out->key = key; return out; }
static void appendMp(strbuf *out, const unsigned char *bytes, size_t size) {
    mp_int *value = mp_from_bytes_be(make_ptrlen(bytes, size));
    put_mp_ssh2(out, value); mp_free(value);
}
static ssh_key *generateRsa(int bits) {
    /* Keep PuTTY's intentional mpunsafe/SSH-client link guard intact.
     * RSA generation uses Windows CNG; only key serialization uses PuTTY. */
    BCRYPT_ALG_HANDLE algorithm = NULL; BCRYPT_KEY_HANDLE key = NULL;
    unsigned char *blob = NULL; ULONG size = 0; ssh_key *result = NULL;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, NULL, 0) < 0 ||
        BCryptGenerateKeyPair(algorithm, &key, bits, 0) < 0 || BCryptFinalizeKeyPair(key, 0) < 0 ||
        BCryptExportKey(key, NULL, BCRYPT_RSAFULLPRIVATE_BLOB, NULL, 0, &size, 0) < 0) goto done;
    blob = (unsigned char *)malloc(size);
    if (!blob || BCryptExportKey(key, NULL, BCRYPT_RSAFULLPRIVATE_BLOB, blob, size, &size, 0) < 0 || size < sizeof(BCRYPT_RSAKEY_BLOB)) goto done;
    BCRYPT_RSAKEY_BLOB *header = (BCRYPT_RSAKEY_BLOB *)blob;
    size_t expected = sizeof(*header) + (size_t)header->cbPublicExp + 2 * (size_t)header->cbModulus + 3 * (size_t)header->cbPrime1 + 2 * (size_t)header->cbPrime2;
    if (header->Magic != BCRYPT_RSAFULLPRIVATE_MAGIC || expected != size) goto done;
    unsigned char *e = blob + sizeof(*header), *n = e + header->cbPublicExp, *p = n + header->cbModulus;
    unsigned char *q = p + header->cbPrime1, *dp = q + header->cbPrime2, *dq = dp + header->cbPrime1;
    unsigned char *iqmp = dq + header->cbPrime2, *d = iqmp + header->cbPrime1;
    strbuf *pub = strbuf_new(), *priv = strbuf_new();
    put_stringz(pub, "ssh-rsa"); appendMp(pub, e, header->cbPublicExp); appendMp(pub, n, header->cbModulus);
    appendMp(priv, d, header->cbModulus); appendMp(priv, p, header->cbPrime1);
    appendMp(priv, q, header->cbPrime2); appendMp(priv, iqmp, header->cbPrime1);
    result = ssh_key_new_priv(&ssh_rsa, ptrlen_from_strbuf(pub), ptrlen_from_strbuf(priv));
    smemclr(priv->s, priv->len); strbuf_free(priv); strbuf_free(pub);
done:
    if (blob) { smemclr(blob, size); free(blob); }
    if (key) BCryptDestroyKey(key);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}
WsKey *wsKeyGenerate(int rsaBits) {
    if (rsaBits != 0 && rsaBits != 3072 && rsaBits != 4096) { fail("Choose Ed25519, RSA 3072, or RSA 4096."); return NULL; }
    random_ref();
    ssh2_userkey *key = snew(ssh2_userkey);
    if (!rsaBits) {
        struct eddsa_key *ed = snew(struct eddsa_key);
        eddsa_generate(ed, 255); key->key = &ed->sshk;
    } else {
        key->key = generateRsa(rsaBits);
        if (!key->key) { sfree(key); random_unref(); fail("Windows could not generate an RSA key."); return NULL; }
    }
    key->comment = dupstr(rsaBits ? "wShell RSA" : "wShell Ed25519");
    random_unref(); return wrap(key);
}
int wsKeyInspect(const wchar_t *path, int *encrypted) {
    if (encrypted) *encrypted = 0;
    Filename *file = filename_from_wstr(path);
    int type = key_type(file), result = WS_KEY_INVALID;
    if (type == SSH_KEYTYPE_SSH2) {
        result = WS_KEY_PPK;
        if (encrypted) *encrypted = ppk_encrypted_f(file, NULL);
    } else if (import_possible(type) && import_target_type(type) == SSH_KEYTYPE_SSH2) {
        result = WS_KEY_IMPORT;
        char *comment = NULL;
        bool protected = import_encrypted(file, type, &comment);
        if (encrypted) *encrypted = protected;
        sfree(comment);
    } else if (type == SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH || type == SSH_KEYTYPE_SSH2_PUBLIC_RFC4716) {
        result = WS_KEY_PUBLIC;
        fail("This file contains only a public key. Select the matching private key (.key, .pem, OpenSSH, or .ppk) to sign in.");
    } else if (type == SSH_KEYTYPE_UNOPENABLE) fail("Cannot read the private key. Check its path and file permissions.");
    else fail("Unsupported private key format. Select a PuTTY PPK, OpenSSH private key, or traditional PEM RSA/DSA/EC private key.");
    filename_free(file);
    return result;
}
WsKey *wsKeyLoad(const wchar_t *path, const char *passphrase) {
    int kind = wsKeyInspect(path, NULL);
    if (kind == WS_KEY_INVALID || kind == WS_KEY_PUBLIC) return NULL;
    Filename *file = filename_from_wstr(path);
    int type = key_type(file);
    const char *error = NULL;
    ssh2_userkey *key = NULL;
    if (type == SSH_KEYTYPE_SSH2) key = ppk_load_f(file, passphrase, &error);
    else if (import_possible(type) && import_target_type(type) == SSH_KEYTYPE_SSH2) {
        char *password = dupstr(passphrase ? passphrase : "");
        key = import_ssh2(file, type, password, &error); smemclr(password, strlen(password)); sfree(password);
    } else error = "Select a PPK or OpenSSH SSH-2 private key.";
    filename_free(file);
    if (!key || key == SSH2_WRONG_PASSPHRASE) { fail(key == SSH2_WRONG_PASSPHRASE ? "The passphrase is incorrect." : error); return NULL; }
    return wrap(key);
}
static PSECURITY_DESCRIPTOR privateDescriptor(void) {
    HANDLE token = NULL; DWORD size = 0; PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return NULL;
    GetTokenInformation(token, TokenUser, NULL, 0, &size);
    TOKEN_USER *user = (TOKEN_USER *)malloc(size);
    LPWSTR sid = NULL;
    if (user && GetTokenInformation(token, TokenUser, user, size, &size) && ConvertSidToStringSidW(user->User.Sid, &sid)) {
        size_t length = wcslen(sid) + 50;
        wchar_t *sddl = (wchar_t *)calloc(length, sizeof(wchar_t));
        if (sddl) {
            swprintf(sddl, length, L"D:P(A;;FA;;;SY)(A;;FA;;;%ls)", sid);
            ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, NULL); free(sddl);
        }
        LocalFree(sid);
    }
    free(user); CloseHandle(token); return descriptor;
}
int wsKeySave(WsKey *key, const wchar_t *path, const char *passphrase) {
    if (!key || !path || !*path) { fail("Choose a key and output file."); return 0; }
    PSECURITY_DESCRIPTOR descriptor = privateDescriptor();
    if (!descriptor) { fail("Cannot create private-key file permissions."); return 0; }
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor, FALSE};
    size_t length = wcslen(path) + 80;
    wchar_t *temp = (wchar_t *)calloc(length, sizeof(wchar_t));
    if (!temp) { LocalFree(descriptor); fail("Out of memory."); return 0; }
    swprintf(temp, length, L"%ls.%lu.%llu.tmp", path, GetCurrentProcessId(), GetTickCount64());
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) { free(temp); fail("Cannot create the private-key file."); return 0; }
    random_ref();
    strbuf *data = ppk_save_sb(key->key, passphrase && *passphrase ? passphrase : NULL, &ppk_save_default_parameters);
    random_unref();
    DWORD written = 0;
    int ok = data && data->len <= MAXDWORD && WriteFile(file, data->s, (DWORD)data->len, &written, NULL) && written == data->len && FlushFileBuffers(file);
    if (data) { smemclr(data->s, data->len); strbuf_free(data); }
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) { DeleteFileW(temp); fail("Could not finish saving the key. Check folder permissions and disk space."); }
    free(temp); return ok;
}
char *wsKeyPublic(WsKey *key) { return key ? ssh2_pubkey_openssh_str(key->key) : NULL; }
char *wsKeyFingerprint(WsKey *key) { return key ? ssh2_fingerprint(key->key->key, SSH_FPTYPE_SHA256) : NULL; }
void wsKeyStringFree(char *text) { if (text) { smemclr(text, strlen(text)); sfree(text); } }
void wsKeyFree(WsKey *key) {
    if (!key) return;
    ssh_key_free(key->key->key); sfree(key->key->comment); sfree(key->key); sfree(key);
}
