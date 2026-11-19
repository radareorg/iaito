# Included by src/Iaito.pro only for iOS.
lessThan(QT_MAJOR_VERSION, 6): error("The iOS build requires Qt 6")
isEmpty(IAITO_IPAD_R2): error("Use scripts/ios.sh to prepare iOS radare2")
!exists($$IAITO_IPAD_R2/iphoneos/lib/libradare2.a): error("Missing iOS radare2 library")
!exists($$IAITO_IPAD_R2/iphonesimulator/lib/libradare2.a): error("Missing simulator radare2 library")

CONFIG += simulator_and_device
CONFIG -= bitcode
QMAKE_INFO_PLIST = $$PWD/Info.plist
QMAKE_IOS_LAUNCH_SCREEN = $$PWD/LaunchScreen.storyboard
QMAKE_APPLE_TARGETED_DEVICE_FAMILY = 1,2
QMAKE_ASSET_CATALOGS += $$PWD/Assets.xcassets
QMAKE_ASSET_CATALOGS_APP_ICON = AppIcon
ICON =

# Xcode selects the correct static library when switching between a device and
# simulator. Both headers describe a 64-bit Darwin build with the same options.
INCLUDEPATH += $$IAITO_IPAD_R2/iphoneos/include/libr
INCLUDEPATH += $$IAITO_IPAD_R2/iphoneos/include/libr/sdb
r2_device.name = IAITO_R2_LIBDIR[sdk=iphoneos*]
r2_device.value = $$IAITO_IPAD_R2/iphoneos/lib
r2_simulator.name = IAITO_R2_LIBDIR[sdk=iphonesimulator*]
r2_simulator.value = $$IAITO_IPAD_R2/iphonesimulator/lib
QMAKE_MAC_XCODE_SETTINGS += r2_device r2_simulator
LIBS += -L$(IAITO_R2_LIBDIR) -lradare2 -lz -liconv

QMAKE_TARGET_BUNDLE_PREFIX = $$section(IAITO_IPAD_BUNDLE_ID, ., 0, -2)
QMAKE_BUNDLE = $$section(IAITO_IPAD_BUNDLE_ID, ., -1, -1)
build_number.name = CURRENT_PROJECT_VERSION
build_number.value = $$IAITO_IPAD_BUILD_NUMBER
enable_bitcode.name = ENABLE_BITCODE
enable_bitcode.value = NO
catalyst.name = SUPPORTS_MACCATALYST
catalyst.value = NO
signing.name = CODE_SIGN_STYLE
signing.value = Automatic
QMAKE_MAC_XCODE_SETTINGS += build_number enable_bitcode catalyst signing

runtime.files = $$IAITO_IPAD_R2/radare2
runtime.path = .
privacy.files = $$PWD/PrivacyInfo.xcprivacy
privacy.path = .
QMAKE_BUNDLE_DATA += runtime privacy
