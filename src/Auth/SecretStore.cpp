/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/SecretStore.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace Auth {

namespace {
#if defined(__APPLE__)
    template <typename T>
    class CfRef {
    public:
        explicit CfRef(T ref = nullptr)
            : m_ref(ref) { }
        ~CfRef() {
            if (m_ref) {
                CFRelease(m_ref);
            }
        }
        CfRef(const CfRef&) = delete;
        CfRef& operator=(const CfRef&) = delete;
        T get() const { return m_ref; }

    private:
        T m_ref;
    };

    class KeychainStore : public SecretStore {
    public:
        KeychainStore(std::string service, std::string account)
            : m_service(std::move(service))
            , m_account(std::move(account)) { }

        bool Save(const std::string& secret) override {
            Erase();
            CfRef<CFMutableDictionaryRef> query(Query());
            CfRef<CFDataRef> data(CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(secret.data()), static_cast<CFIndex>(secret.size())));
            CFDictionarySetValue(query.get(), kSecValueData, data.get());
            return SecItemAdd(query.get(), nullptr) == errSecSuccess;
        }

        std::optional<std::string> Load() override {
            CfRef<CFMutableDictionaryRef> query(Query());
            CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
            CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
            CFTypeRef found = nullptr;
            if (SecItemCopyMatching(query.get(), &found) != errSecSuccess || !found) {
                return std::nullopt;
            }
            CfRef<CFDataRef> data(static_cast<CFDataRef>(found));
            return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data.get())), static_cast<size_t>(CFDataGetLength(data.get())));
        }

        void Erase() override {
            CfRef<CFMutableDictionaryRef> query(Query());
            SecItemDelete(query.get());
        }

    private:
        CFMutableDictionaryRef Query() const {
            CFMutableDictionaryRef query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CfRef<CFStringRef> service(CFStringCreateWithCString(nullptr, m_service.c_str(), kCFStringEncodingUTF8));
            CfRef<CFStringRef> account(CFStringCreateWithCString(nullptr, m_account.c_str(), kCFStringEncodingUTF8));
            CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
            CFDictionarySetValue(query, kSecAttrService, service.get());
            CFDictionarySetValue(query, kSecAttrAccount, account.get());
            return query;
        }

        std::string m_service;
        std::string m_account;
    };
#else
    class UnsupportedStore : public SecretStore {
    public:
        bool Save(const std::string&) override { return false; }
        std::optional<std::string> Load() override { return std::nullopt; }
        void Erase() override { }
    };
#endif
}

std::unique_ptr<SecretStore> MakeSystemSecretStore(const std::string& service, const std::string& account) {
#if defined(__APPLE__)
    return std::make_unique<KeychainStore>(service, account);
#else
    (void)service;
    (void)account;
    return std::make_unique<UnsupportedStore>();
#endif
}

}
