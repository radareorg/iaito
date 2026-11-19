# Invoked by scripts/ios.sh. Keep qmake and compilation behind make targets.
.PHONY: check-qt radare2 project build simulator archive export

check-qt:
	@python3 "$(ROOT)/dist/ipad/prepare.py" check-qt "$(QT_IOS)" "$(IOS_MIN_VERSION)"

radare2:
	@test -f "$(R2_SOURCE)/subprojects/sdb/meson.build" && \
		test -f "$(R2_SOURCE)/subprojects/otezip/meson.build" || \
		meson subprojects download -j1 --sourcedir "$(R2_SOURCE)" sdb otezip \
		> "$(R2_BUILD)/download.log" 2>&1 \
		|| { tail -60 "$(R2_BUILD)/download.log" >&2; exit 1; }
	python3 "$(ROOT)/dist/ipad/prepare.py" radare2 "$(R2_SOURCE)"
	@python3 "$(ROOT)/dist/ipad/prepare.py" libraries "$(R2_SOURCE)" "$(R2_BUILD)" \
		"$(R2_COMMIT)" "$(IOS_MIN_VERSION)" "$(SIMULATOR_ARCHS)" "$(JOBS)" \
		> "$(R2_BUILD)/build.log" 2>&1 \
		|| { tail -60 "$(R2_BUILD)/build.log" >&2; exit 1; }

project:
	@mkdir -p "$(BUILD_DIR)/iaito"
	@cd "$(BUILD_DIR)/iaito" && python3 "$(ROOT)/dist/ipad/prepare.py" qmake \
		"$(QT_IOS)" "$(ROOT)/src/Iaito.pro" \
		-spec macx-xcode -o "$(ROOT)/dist/ipad/iaito.xcodeproj" \
		"IAITO_IPAD_CONFIG=$(BUILD_DIR)/ipad.pri" \
		> "$(BUILD_DIR)/iaito-configure.log" 2>&1 \
		|| { tail -60 "$(BUILD_DIR)/iaito-configure.log" >&2; exit 1; }

build simulator archive export:
	@python3 "$(ROOT)/dist/ipad/prepare.py" build "$(ROOT)" "$(BUILD_DIR)" "$@" "$(JOBS)" \
		> "$(BUILD_DIR)/iaito-build.log" 2>&1 \
		|| { tail -80 "$(BUILD_DIR)/iaito-build.log" >&2; exit 1; }
