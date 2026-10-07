// swift-tools-version:6.0
import PackageDescription

let package = Package(
    name: "wShell",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "wShell", targets: ["WShell"])],
    dependencies: [.package(url: "https://github.com/migueldeicaza/SwiftTerm.git",
                            revision: "464df5207fc2432e16c9a23abe538187196daf5f")],
    targets: [
        .target(name: "WShellCore"),
        .executableTarget(name: "WShell", dependencies: ["WShellCore", "SwiftTerm"]),
        .testTarget(name: "WShellCoreTests", dependencies: ["WShellCore"])
    ],
    swiftLanguageModes: [.v5]
)
