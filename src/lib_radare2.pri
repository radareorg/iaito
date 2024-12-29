win32 {
    DEFINES += _CRT_NONSTDC_NO_DEPRECATE
    DEFINES += _CRT_SECURE_NO_WARNINGS
    LIBS += -L"$$PWD/../radare2/lib"
    R2_INCLUDEPATH += "$$PWD/../radare2/include/libr"
    R2_INCLUDEPATH += "$$PWD/../radare2/include/libr/sdb"
    INCLUDEPATH += $$R2_INCLUDEPATH

    LIBS += \
        -lr_core \
        -lr_config \
        -lr_arch \
        -lr_cons \
        -lr_io \
        -lr_util \
        -lr_flag \
        -lr_asm \
        -lr_debug \
        -lr_hash \
        -lr_bin \
        -lr_lang \
        -lr_anal \
        -lr_parse \
        -lr_bp \
        -lr_egg \
        -lr_reg \
        -lr_search \
        -lr_syscall \
        -lr_socket \
        -lr_fs \
        -lr_magic \
        -lr_crypto
} else {
    macx|bsd {
        R2PREFIX=/usr/local
        USE_PKGCONFIG = 0
    } else {
        R2PREFIX=/usr
        USE_PKGCONFIG = 0
    }
    R2_USER_PKGCONFIG = $$(HOME)/bin/prefix/radare2/lib/pkgconfig
    system("pkg-config --exists r_core") : {
        USE_PKGCONFIG=1
    } else : exists($$R2_USER_PKGCONFIG) {
        # caution: may not work for cross compilations
        PKG_CONFIG_PATH=$$PKG_CONFIG_PATH:$$R2_USER_PKGCONFIG
        USE_PKGCONFIG=1
    } else {
        unix {
            exists($$R2PREFIX/lib/pkgconfig/r_core.pc) {
                USE_PKGCONFIG = 1
                PKG_CONFIG_PATH=$$PKG_CONFIG_PATH:$$R2PREFIX/lib/pkgconfig
            } else {
                R2_INCLUDEPATH += $$R2PREFIX/include/libr
                R2_INCLUDEPATH += $$R2PREFIX/include/libr/sdb
                USE_PKGCONFIG = 0
            }
        }
        macx {
            LIBS += -L$$R2PREFIX/lib
            R2_INCLUDEPATH += $$R2PREFIX/include/libr
            R2_INCLUDEPATH += $$R2PREFIX/include/libr/sdb
            USE_PKGCONFIG = 0
        }
        bsd {
            !exists($$PKG_CONFIG_PATH/r_core.pc) {
                LIBS += -L$$R2PREFIX/lib
                R2_INCLUDEPATH += $$R2PREFIX/include/libr
                R2_INCLUDEPATH += $$R2PREFIX/include/libr/sdb
                USE_PKGCONFIG = 0
            }
        }
    }
    INCLUDEPATH += $$R2_INCLUDEPATH

    DEFINES += _CRT_NONSTDC_NO_DEPRECATE
    DEFINES += _CRT_SECURE_NO_WARNINGS
    equals(USE_PKGCONFIG, 1) {
        CONFIG += link_pkgconfig
       # PKGCONFIG += r_core
        R2_INCLUDEPATH = "$$system("bash -c 'pkg-config --variable=includedir r_core'")/libr"
        R2_INCLUDEPATH += "$$system("bash -c 'pkg-config --variable=includedir r_core'")/libr/sdb"
        INCLUDEPATH += $$R2_INCLUDEPATH
        LIBS += $$system("pkg-config --libs r_core")
    } else {
        LIBS += -L$$R2PREFIX/lib
        LIBS += \
        -lr_core \
        -lr_config \
        -lr_cons \
        -lr_arch \
        -lr_io \
        -lr_flag \
        -lr_asm \
        -lr_debug \
        -lr_hash \
        -lr_bin \
        -lr_lang \
        -lr_parse \
        -lr_bp \
        -lr_egg \
        -lr_reg \
        -lr_search \
        -lr_syscall \
        -lr_socket \
        -lr_fs \
        -lr_anal \
        -lr_magic \
        -lr_util \
        -lr_crypto
    }
}
