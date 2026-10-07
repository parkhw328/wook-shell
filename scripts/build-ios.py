"""Build the personal iPad preview from pinned sources on macOS; never signs for a user."""
from pathlib import Path
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/ios'
VERSION = (ROOT / 'ios/VERSION').read_text().strip()
SOURCES = {
    'openssl': ('https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz',
                '603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a'),
    'libssh2': ('https://codeload.github.com/libssh2/libssh2/tar.gz/d4e5780315c352a8b2957eb95072d74ff1cdcc7d',
                '528d36f9c8e2118e5b2ee545460f77f19dd07f896a69415542f290495768c2ee'),
}

def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)

def source(name):
    url, digest = SOURCES[name]
    archive = BUILD / (name + '.tar.gz')
    if not archive.exists():
        urllib.request.urlretrieve(url, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'{name} checksum mismatch')
    dest = BUILD / 'sources' / name
    if not dest.exists():
        dest.mkdir(parents=True)
        with tarfile.open(archive) as tar:
            prefix = tar.getmembers()[0].name.split('/')[0] + '/'
            for item in tar.getmembers():
                if not item.name.startswith(prefix):
                    continue
                item.name = item.name[len(prefix):]
                if item.name:
                    tar.extract(item, dest, filter='data')
    return dest

def dependencies(sdk):
    prefix = BUILD / 'deps' / sdk
    marker = prefix / 'pin.json'
    pin = json.dumps(SOURCES, sort_keys=True)
    if marker.exists() and marker.read_text() == pin:
        return
    ssl = source('openssl')
    ssh = source('libssh2')
    work = BUILD / ('openssl-' + sdk)
    work.mkdir(exist_ok=True)
    sysroot = subprocess.check_output(['xcrun', '--sdk', sdk, '--show-sdk-path'], text=True).strip()
    target = 'ios64-xcrun' if sdk == 'iphoneos' else 'iossimulator-xcrun'
    env = dict(os.environ, SDKROOT=sysroot, CFLAGS='-arch arm64', LDFLAGS='-arch arm64')
    run('perl', ssl / 'Configure', target, 'no-shared', 'no-tests', 'no-apps', 'no-module',
        'no-dso', 'no-engine', '--prefix=' + str(prefix),
        '-miphoneos-version-min=17.0' if sdk == 'iphoneos' else '-mios-simulator-version-min=17.0', cwd=work, env=env)
    run('make', '-j', '4', cwd=work, env=env)
    run('make', 'install_sw', cwd=work, env=env)
    cmake = BUILD / ('libssh2-' + sdk)
    run('cmake', '-S', ssh, '-B', cmake, '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_OSX_SYSROOT=' + sysroot, '-DCMAKE_OSX_DEPLOYMENT_TARGET=17.0',
        '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-DCMAKE_INSTALL_LIBDIR=lib',
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_EXAMPLES=OFF',
        '-DBUILD_TESTING=OFF', '-DLIBSSH2_BUILD_DOCS=OFF', '-DCRYPTO_BACKEND=OpenSSL',
        '-DLIBSSH2_USE_PKGCONFIG=OFF', '-DOPENSSL_INCLUDE_DIR=' + str(prefix / 'include'),
        '-DOPENSSL_CRYPTO_LIBRARY=' + str(prefix / 'lib/libcrypto.a'),
        '-DOPENSSL_SSL_LIBRARY=' + str(prefix / 'lib/libssl.a'))
    run('cmake', '--build', cmake, '-j', '4')
    run('cmake', '--install', cmake)
    marker.write_text(pin)

def project():
    """Generate an Xcode project without a third-party project generator."""
    objects = {}
    def obj(identifier, isa, **values):
        key = hashlib.sha1(identifier.encode()).hexdigest()[:24].upper()
        objects[key] = dict(isa=isa, **values)
        return key
    def config(name, settings):
        configs = [obj(name + c, 'XCBuildConfiguration', name=c, buildSettings=settings) for c in ('Debug', 'Release')]
        return obj(name, 'XCConfigurationList', buildConfigurations=configs, defaultConfigurationIsVisible=0, defaultConfigurationName='Release')
    files, sources, resources = [], [], []
    for p in sorted((ROOT / 'ios').glob('*.swift')) + [ROOT / 'mac/Sources/WShellCore/SplitLayout.swift', ROOT / 'ios/SSH.c', ROOT / 'mac/Sources/CSFTP/wsftp.c']:
        ref = obj(str(p), 'PBXFileReference', path=str(p), sourceTree='<absolute>', lastKnownFileType='sourcecode.swift' if p.suffix == '.swift' else 'sourcecode.c.c')
        files.append(ref); sources.append(obj(str(p) + 'build', 'PBXBuildFile', fileRef=ref))
    assets = BUILD / 'Assets.xcassets'; icons = assets / 'AppIcon.appiconset'; icons.mkdir(parents=True, exist_ok=True)
    # Reuse the existing wShell artwork; no separate visual identity.
    run('sips', '-z', '1024', '1024', ROOT / 'assets/branding/wshell-icon.png', '--out', icons / 'Icon.png', stdout=subprocess.DEVNULL)
    (icons / 'Contents.json').write_text(json.dumps({'images':[{'filename':'Icon.png','idiom':'universal','platform':'ios','size':'1024x1024'}], 'info':{'author':'xcode','version':1}}))
    (assets / 'Contents.json').write_text(json.dumps({'info':{'author':'xcode','version':1}}))
    notices = BUILD / 'legal-notices.txt'
    text = [(ROOT / 'LICENSE').read_text()]
    for name in ['SwiftTerm.txt', 'Flexoki-MIT.txt', 'JetBrainsMono-OFL.txt', 'libssh2.txt', 'OpenSSL.txt']:
        text.append(name + '\n\n' + (ROOT / 'licenses' / name).read_text())
    notices.write_text('\n\n'.join(text))
    for p in [*sorted((ROOT / 'assets/fonts').glob('*.ttf')), notices, assets]:
        ref = obj(str(p), 'PBXFileReference', path=str(p), sourceTree='<absolute>', lastKnownFileType='folder.assetcatalog' if p == assets else 'file')
        files.append(ref); resources.append(obj(str(p) + 'build', 'PBXBuildFile', fileRef=ref))
    app = obj('app', 'PBXFileReference', path='wShell.app', sourceTree='BUILT_PRODUCTS_DIR', explicitFileType='wrapper.application')
    group = obj('group', 'PBXGroup', children=files + [app], sourceTree='<group>')
    package = obj('SwiftTermPackage', 'XCRemoteSwiftPackageReference', repositoryURL='https://github.com/migueldeicaza/SwiftTerm.git', requirement={'kind':'revision', 'revision':'464df5207fc2432e16c9a23abe538187196daf5f'})
    product = obj('SwiftTermProduct', 'XCSwiftPackageProductDependency', package=package, productName='SwiftTerm')
    framework = obj('SwiftTermBuild', 'PBXBuildFile', productRef=product)
    phases = [obj('sources', 'PBXSourcesBuildPhase', files=sources, buildActionMask=2147483647, runOnlyForDeploymentPostprocessing=0),
              obj('resources', 'PBXResourcesBuildPhase', files=resources, buildActionMask=2147483647, runOnlyForDeploymentPostprocessing=0),
              obj('frameworks', 'PBXFrameworksBuildPhase', files=[framework], buildActionMask=2147483647, runOnlyForDeploymentPostprocessing=0)]
    info = {'CFBundleName':'wShell', 'CFBundleDisplayName':'wShell', 'CFBundleExecutable':'$(EXECUTABLE_NAME)',
            'CFBundleIdentifier':'$(PRODUCT_BUNDLE_IDENTIFIER)', 'CFBundlePackageType':'APPL',
            'CFBundleShortVersionString':VERSION, 'CFBundleVersion':'1', 'LSRequiresIPhoneOS':True,
            'UILaunchScreen':{}, 'UISupportedInterfaceOrientations':['UIInterfaceOrientationPortrait','UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'],
            'UIAppFonts':['JetBrainsMono-Regular.ttf','JetBrainsMono-Bold.ttf'],
            'UIFileSharingEnabled':True, 'LSSupportsOpeningDocumentsInPlace':True,
            'NSLocalNetworkUsageDescription':'Connect to SSH and SFTP servers on your local network.',
            'NSHumanReadableCopyright':'Created by Hyunwook Park. MIT License.'}
    (BUILD / 'Info.plist').write_bytes(plistlib.dumps(info))
    settings = {'PRODUCT_NAME':'wShell', 'PRODUCT_BUNDLE_IDENTIFIER':'com.wshell.ipad', 'SWIFT_VERSION':'5.0',
                'IPHONEOS_DEPLOYMENT_TARGET':'17.0', 'TARGETED_DEVICE_FAMILY':'2', 'SDKROOT':'iphoneos',
                'SUPPORTED_PLATFORMS':'iphoneos iphonesimulator', 'CODE_SIGNING_ALLOWED':'NO',
                'INFOPLIST_FILE':str(BUILD / 'Info.plist'), 'SWIFT_OBJC_BRIDGING_HEADER':str(ROOT / 'ios/Bridge.h'),
                'HEADER_SEARCH_PATHS':[str(ROOT / 'ios'), str(ROOT / 'mac/Sources/CSFTP/include'), str(BUILD / 'sources/libssh2/include')],
                'LIBRARY_SEARCH_PATHS':str(BUILD / 'deps/$(PLATFORM_NAME)/lib'),
                'OTHER_LDFLAGS':['-lssh2','-lcrypto','-lz'], 'CLANG_ENABLE_MODULES':'YES',
                'ASSETCATALOG_COMPILER_APPICON_NAME':'AppIcon', 'SWIFT_OPTIMIZATION_LEVEL':'-O',
                'ENABLE_USER_SCRIPT_SANDBOXING':'YES', 'DEBUG_INFORMATION_FORMAT':'dwarf-with-dsym'}
    target = obj('target', 'PBXNativeTarget', name='wShell', productName='wShell', productReference=app, productType='com.apple.product-type.application', buildConfigurationList=config('targetConfig', settings), buildPhases=phases, buildRules=[], dependencies=[], packageProductDependencies=[product])
    root = obj('project', 'PBXProject', attributes={'LastUpgradeCheck':'1600'}, buildConfigurationList=config('projectConfig', {}), compatibilityVersion='Xcode 14.0', developmentRegion='en', knownRegions=['en','Base'], mainGroup=group, productRefGroup=group, projectDirPath='', projectRoot='', targets=[target], packageReferences=[package])
    directory = BUILD / 'wShell.xcodeproj'; directory.mkdir(exist_ok=True)
    (directory / 'project.pbxproj').write_bytes(plistlib.dumps(dict(archiveVersion='1', classes={}, objectVersion='56', objects=objects, rootObject=root)))
    resolved = directory / 'project.xcworkspace/xcshareddata/swiftpm/Package.resolved'
    resolved.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'mac/Package.resolved', resolved)
    return directory

def main():
    if sys.platform != 'darwin':
        raise SystemExit('Requires macOS with Xcode. Use the iPad personal preview workflow on Windows.')
    BUILD.mkdir(parents=True, exist_ok=True)
    for sdk in ('iphonesimulator', 'iphoneos'):
        dependencies(sdk)
    directory = project()
    for sdk in ('iphonesimulator', 'iphoneos'):
        # The pinned SwiftTerm build-info plugin was reviewed; it only emits revision metadata.
        run('xcodebuild', '-skipPackagePluginValidation', '-disableAutomaticPackageResolution', '-project', directory, '-scheme', 'wShell', '-configuration', 'Release',
            '-sdk', sdk, '-destination', 'generic/platform=iOS Simulator' if sdk == 'iphonesimulator' else 'generic/platform=iOS',
            '-derivedDataPath', BUILD / 'DerivedData', 'ARCHS=arm64', 'CODE_SIGNING_ALLOWED=NO', 'build')
    app = BUILD / 'DerivedData/Build/Products/Release-iphoneos/wShell.app'
    output = ROOT / 'dist/ipad' / VERSION; output.mkdir(parents=True, exist_ok=True)
    archive = output / f'wshell-ipad-{VERSION}-unsigned.ipa'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in app.rglob('*'):
            if p.is_file(): z.write(p, 'Payload/wShell.app/' + p.relative_to(app).as_posix())
    archive.with_suffix('.ipa.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')
    print('Built unsigned personal preview. User signing is REQUIRED before installation.')

if __name__ == '__main__': main()
