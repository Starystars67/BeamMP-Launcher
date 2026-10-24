find_package(doctest CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(CURL REQUIRED)

add_executable(LauncherTests
    Main.cpp
    Stubs.cpp
    LoggerTests.cpp
    OptionsTests.cpp
    StatusTests.cpp
    PkceTests.cpp
    ObserversTests.cpp
    ProtocolTests.cpp
    RelayProtocolTests.cpp
    Base64Tests.cpp
    SessionDataTests.cpp
    LifecycleTests.cpp
    ../src/Logger.cpp
    ../src/Options.cpp
    ../src/Status.cpp
    ../src/Auth/Pkce.cpp
    ../src/Auth/OAuth.cpp
    ../src/Auth/CurlTransport.cpp
    ../src/Auth/CallbackServer.cpp
    ../src/Auth/SecretStore.cpp
    ../src/Auth/Session.cpp
    ../src/Control/Protocol.cpp
    ../src/Control/Lifecycle.cpp
    ../src/Control/Server.cpp
    ../src/Control/Controller.cpp
    ../src/Games/GameBridge.cpp
    ../src/Games/Registry.cpp
    ../src/Games/GearBlocks.cpp
    ../src/Games/GearBlocksInstaller.cpp
    ../src/Games/GearBlocksManifest.cpp
    ../src/Games/Process.cpp
    ../src/Games/ManagedProcess.cpp
    ../src/Games/Preparer.cpp
    ../src/Games/RelayProtocol.cpp
    ../src/Games/RelayLink.cpp
    ../src/Games/RelaySession.cpp
    ../src/Games/SessionData.cpp
    ../src/Net/Base64.cpp
    ../src/Net/Sha256.cpp
    ../src/Net/Download.cpp
    ../src/Net/Unzip.cpp
    ../src/Net/Ed25519.cpp)
# The socket tests use a POSIX client.
if(NOT WIN32)
    target_sources(LauncherTests PRIVATE ServerTests.cpp ControllerTests.cpp TestClient.cpp AuthSupport.cpp OAuthApiTests.cpp CallbackServerTests.cpp CurlTransportTests.cpp SessionTests.cpp KeychainTests.cpp GameBridgeTests.cpp GearBlocksTests.cpp GameRegistryTests.cpp ManagedProcessTests.cpp PreparerTests.cpp RelayLinkTests.cpp RelaySessionTests.cpp DownloadTests.cpp UnzipTests.cpp GearBlocksInstallerTests.cpp GearBlocksManifestTests.cpp Ed25519Tests.cpp)
endif()
target_include_directories(LauncherTests PRIVATE ../include)
get_target_property(HTTPLIB_INCLUDE_DIRS httplib::httplib INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(LauncherTests PRIVATE ${HTTPLIB_INCLUDE_DIRS})
target_compile_definitions(LauncherTests PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(LauncherTests PRIVATE doctest::doctest OpenSSL::SSL OpenSSL::Crypto Threads::Threads nlohmann_json::nlohmann_json CURL::libcurl)
if (APPLE)
    target_link_libraries(LauncherTests PRIVATE "-framework CoreFoundation" "-framework Security")
endif()

add_test(NAME LauncherTests COMMAND LauncherTests)
