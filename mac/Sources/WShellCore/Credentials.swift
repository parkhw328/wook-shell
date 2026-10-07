import Foundation
import Security

public final class CredentialStore {
    let service: String
    public init(service: String = "com.wshell.ssh-password") { self.service = service }
    func query(_ account: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
         kSecAttrAccount as String: account, kSecAttrSynchronizable as String: false]
    }
    public func read(_ account: String) throws -> Data? {
        var attributes = query(account)
        attributes[kSecReturnData as String] = true; attributes[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        let status = SecItemCopyMatching(attributes as CFDictionary, &item)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess else { throw WShellError("Cannot read the login Keychain (\(status)).") }
        return item as? Data
    }
    public func contains(_ account: String) -> Bool {
        var attributes = query(account); attributes[kSecReturnAttributes as String] = true
        attributes[kSecUseAuthenticationUI as String] = kSecUseAuthenticationUIFail
        return SecItemCopyMatching(attributes as CFDictionary, nil) == errSecSuccess
    }
    public func save(_ account: String, password: Data) throws {
        let attributes: [String: Any] = [kSecValueData as String: password,
            kSecAttrAccessible as String: kSecAttrAccessibleWhenUnlockedThisDeviceOnly]
        var status = SecItemUpdate(query(account) as CFDictionary, attributes as CFDictionary)
        if status == errSecItemNotFound {
            status = SecItemAdd(query(account).merging(attributes) { _, new in new } as CFDictionary, nil)
        }
        guard status == errSecSuccess else { throw WShellError("Cannot save the password in the login Keychain (\(status)).") }
    }
    public func remove(_ account: String) throws {
        let status = SecItemDelete(query(account) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else { throw WShellError("Cannot remove the Keychain password (\(status)).") }
    }
}
