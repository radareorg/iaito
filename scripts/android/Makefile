# Invoked by ../android.sh. Keep qmake and compilation behind make targets.
.PHONY: radare2 apk

radare2:
	@test -f "$(BUILD_DIR)/radare2/subprojects/sdb/meson.build" || \
		meson subprojects download -j1 --sourcedir "$(BUILD_DIR)/radare2" sdb \
		> "$(BUILD_DIR)/radare2-download.log" 2>&1 \
		|| { tail -60 "$(BUILD_DIR)/radare2-download.log" >&2; exit 1; }
	python3 "$(ROOT)/scripts/android/prepare.py" --radare2 "$(BUILD_DIR)/radare2"
	@meson setup $(if $(wildcard $(BUILD_DIR)/r2-build/meson-private/coredata.dat),--reconfigure) \
		"$(BUILD_DIR)/r2-build" "$(BUILD_DIR)/radare2" \
		--cross-file "$(BUILD_DIR)/android.ini" --prefix "$(BUILD_DIR)/prefix" --libdir lib \
		--buildtype release -Ddefault_library=shared -Dcli=disabled \
		-Denable_tests=false -Denable_r2r=false -Duse_webui=false -Ddebugger=false \
		-Duse_fork=true -Dstatic_themes=true > "$(BUILD_DIR)/radare2-configure.log" 2>&1 \
		|| { tail -60 "$(BUILD_DIR)/radare2-configure.log" >&2; exit 1; }
	@meson compile -C "$(BUILD_DIR)/r2-build" -j$(JOBS) \
		> "$(BUILD_DIR)/radare2-build.log" 2>&1 \
		|| { tail -60 "$(BUILD_DIR)/radare2-build.log" >&2; exit 1; }
	@meson install -C "$(BUILD_DIR)/r2-build" --no-rebuild \
		> "$(BUILD_DIR)/radare2-install.log" 2>&1 \
		|| { tail -60 "$(BUILD_DIR)/radare2-install.log" >&2; exit 1; }

apk:
	mkdir -p "$(BUILD_DIR)/iaito"
	@cd "$(BUILD_DIR)/iaito" && "$(QT_ANDROID)/bin/qmake" "$(ROOT)/src/Iaito.pro" \
		-spec android-clang -config release "ANDROID_ABIS=$(ANDROID_ABI)" \
		"ANDROID_PLATFORM=android-$(ANDROID_MIN_SDK)" \
		"ANDROID_MIN_SDK_VERSION=$(ANDROID_MIN_SDK)" \
		"ANDROID_TARGET_SDK_VERSION=$(ANDROID_TARGET_SDK)" \
		"ANDROID_VERSION_CODE=$(ANDROID_VERSION_CODE)" \
		"IAITO_ANDROID_R2_PREFIX=$(BUILD_DIR)/prefix" \
		"ANDROID_PACKAGE_SOURCE_DIR=$(BUILD_DIR)/package" \
		IAITO_ENABLE_DEBUGGER=false IAITO_ENABLE_CRASH_REPORTS=false \
		> "$(BUILD_DIR)/iaito-configure.log" 2>&1 \
		|| { tail -60 "$(BUILD_DIR)/iaito-configure.log" >&2; exit 1; }
	@$(MAKE) -C "$(BUILD_DIR)/iaito" apk > "$(BUILD_DIR)/iaito-build.log" 2>&1 \
		|| { tail -60 "$(BUILD_DIR)/iaito-build.log" >&2; exit 1; }
