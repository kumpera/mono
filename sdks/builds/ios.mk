#
# Targets:
# - build-ios-<target>
#    Build <target>
# - package-ios-<target>
#    Install target into ../out/<target>
# - clean-ios-<target>
#    Clean target
# Where <target> is: target32, target64, sim32, sim64, cross32, cross64
#

PLATFORM_BIN=$(XCODE_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/bin

ios_CFLAGS= \
	$(if $(filter $(RELEASE),true),-O2,-O0 -ggdb3 -gdwarf-2) \
	-DMONOTOUCH=1

ios_CPPFLAGS= \
	$(if $(filter $(RELEASE),true),-O2,-O0 -ggdb3 -gdwarf-2) \
	-DMONOTOUCH=1

ios_CXXFLAGS= \
	$(if $(filter $(RELEASE),true),-O2,-O0 -ggdb3 -gdwarf-2) \
	-DMONOTOUCH=1

ios_LDFLAGS=

##
# Parameters
#  $(1): target (target32 or target64)
#  $(2): arch (armv7 or arm64)
#  $(3): arch (arm or aarch64)
#
# Flags:
#  ios_$(1)_AC_VARS
#  ios_$(1)_CFLAGS
#  ios_$(1)_CPPFLAGS
#  ios_$(1)_CXXFLAGS
#  ios_$(1)_LDFLAGS
#  ios_$(1)_BITCODE_MARKER
define iOSDeviceTemplate

_ios_$(1)_CC=$$(CCACHE) $$(PLATFORM_BIN)/clang
_ios_$(1)_CXX=$$(CCACHE) $$(PLATFORM_BIN)/clang++

_ios_$(1)_AC_VARS= \
	ac_cv_c_bigendian=no \
	ac_cv_func_finite=no \
	ac_cv_func_getpwuid_r=no \
	ac_cv_func_posix_getpwuid_r=yes \
	ac_cv_header_curses_h=no \
	ac_cv_header_localcharset_h=no \
	ac_cv_header_sys_user_h=no \
	ac_cv_func_getentropy=no \
	ac_cv_func_futimens=no \
	ac_cv_func_utimensat=no \
	mono_cv_sizeof_sunpath=104 \
	mono_cv_uscore=yes \
	$$(ios_$(1)_AC_VARS)

_ios_$(1)_CFLAGS= \
	$$(ios_CFLAGS) \
	-isysroot $(XCODE_DIR)/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS$$(IOS_VERSION).sdk -miphoneos-version-min=$$(IOS_VERSION_MIN) \
	-Wl,-application_extension \
	-fexceptions \
	-DSMALL_CONFIG -DDISABLE_POLICY_EVIDENCE=1 -DDISABLE_PROCESS_HANDLING=1 -D_XOPEN_SOURCE -DHOST_IOS -DHAVE_LARGE_FILE_SUPPORT=1 \
	$$(ios_$(1)_BITCODE_MARKER) \
	$$(ios_$(1)_CFLAGS)

_ios_$(1)_CPPFLAGS= \
	$$(ios_CPPFLAGS) \
	-isysroot $(XCODE_DIR)/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS$$(IOS_VERSION).sdk -miphoneos-version-min=$$(IOS_VERSION_MIN) \
	-arch $(2) \
	-Wl,-application_extension \
	-DSMALL_CONFIG -DDISABLE_POLICY_EVIDENCE=1 -DDISABLE_PROCESS_HANDLING=1 -D_XOPEN_SOURCE -DHOST_IOS -DHAVE_LARGE_FILE_SUPPORT=1 \
	$$(ios_$(1)_BITCODE_MARKER) \
	$$(ios_$(1)_CPPFLAGS)

_ios_$(1)_CXXFLAGS= \
	$$(ios_CXXFLAGS) \
	-isysroot $(XCODE_DIR)/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS$$(IOS_VERSION).sdk -miphoneos-version-min=$$(IOS_VERSION_MIN) \
	-arch $(2) \
	-Wl,-application_extension \
	-DSMALL_CONFIG -DDISABLE_POLICY_EVIDENCE=1 -DDISABLE_PROCESS_HANDLING=1 -D_XOPEN_SOURCE -DHOST_IOS -DHAVE_LARGE_FILE_SUPPORT=1 \
	$$(ios_$(1)_BITCODE_MARKER) \
	$$(ios_$(1)_CPPFLAGS)

_ios_$(1)_LDFLAGS= \
	$$(ios_LDFLAGS) \
	-Wl,-no_weak_imports \
	-arch $(2) \
	-framework CoreFoundation \
	-lobjc -lc++ \
	$$(ios_$(1)_LDFLAGS)

_ios_$(1)_CONFIGURE_ENVIRONMENT = \
	CC="$$(_ios_$(1)_CC)" \
	CXX="$$(_ios_$(1)_CXX)" \
	CFLAGS="$$(_ios_$(1)_CFLAGS)" \
	CPPFLAGS="$$(_ios_$(1)_CPPFLAGS)" \
	CXXFLAGS="$$(_ios_$(1)_CXXFLAGS)" \
	LDFLAGS="$$(_ios_$(1)_LDFLAGS)"

_ios_$(1)_CONFIGURE_FLAGS = \
	--build=i386-apple-darwin10 \
	--host=$(3)-apple-darwin10 \
	--cache-file=$(TOP)/sdks/builds/ios-$(1).config.cache \
	--prefix=$(TOP)/sdks/out/ios-$(1) \
	--disable-boehm \
	--disable-btls \
	--disable-executables \
	--disable-icall-tables \
	--disable-iconv \
	--disable-mcs-build \
	--disable-nls \
	--disable-support-build \
	--disable-visibility-hidden \
	--enable-dtrace=no \
	--enable-icall-export \
	--enable-maintainer-mode \
	--enable-minimal=ssa,com,jit,reflection_emit_save,reflection_emit,portability,assembly_remapping,attach,verifier,full_messages,appdomains,security,sgen_remset,sgen_marksweep_par,sgen_marksweep_fixed,sgen_marksweep_fixed_par,sgen_copying,logging,remoting,shared_perfcounters \
	--with-lazy-gc-thread-creation=yes \
	--with-monotouch \
	--with-tls=pthread \
	--without-ikvm-native \
	--without-sigaltstack

.stamp-ios-$(1)-toolchain:
	touch $$@

.stamp-ios-$(1)-configure: $$(TOP)/configure
	mkdir -p $$(TOP)/sdks/builds/ios-$(1)
	cd $$(TOP)/sdks/builds/ios-$(1) && PATH="$$(PLATFORM_BIN):$$$$PATH" $$(TOP)/configure $$(_ios_$(1)_AC_VARS) $$(_ios_$(1)_CONFIGURE_ENVIRONMENT) $$(_ios_$(1)_CONFIGURE_FLAGS)
	touch $$@

.PHONY: package-ios-$(1)
package-ios-$(1):
	$(MAKE) -C $$(TOP)/sdks/builds/ios-$(1)/mono install

.PHONY: clean-ios-$(1)
clean-ios-$(1):
	rm -rf .stamp-ios-$(1)-toolchain .stamp-ios-$(1)-configure $$(TOP)/sdks/builds/ios-$(1) $$(TOP)/sdks/builds/ios-$(1).config.cache $$(TOP)/sdks/out/ios-$(1)

TARGETS += ios-$(1)

endef

# ios_target32_BITCODE_MARKER=-fembed-bitcode-marker
$(eval $(call iOSDeviceTemplate,target32,armv7,arm))
# ios_target64_BITCODE_MARKER=-fembed-bitcode-marker
$(eval $(call iOSDeviceTemplate,target64,arm64,aarch64))

##
# Parameters
#  $(1): target (sim32 or sim64)
#  $(2): arch (i386 or x86_64)
#
# Flags:
#  ios_$(1)_AC_VARS
#  ios_$(1)_CFLAGS
#  ios_$(1)_CPPFLAGS
#  ios_$(1)_CXXFLAGS
#  ios_$(1)_LDFLAGS
define iOSSimulatorTemplate

_ios_$(1)_CC=$$(CCACHE) $$(PLATFORM_BIN)/clang
_ios_$(1)_CXX=$$(CCACHE) $$(PLATFORM_BIN)/clang++

_ios_$(1)_AC_VARS= \
	ac_cv_func_clock_nanosleep=no \
	ac_cv_func_fstatat=no \
	ac_cv_func_readlinkat=no \
	ac_cv_func_system=no \
	ac_cv_func_getentropy=no \
	ac_cv_func_futimens=no \
	ac_cv_func_utimensat=no \
	mono_cv_uscore=yes \
	$(ios_$(1)_AC_VARS)

_ios_$(1)_CFLAGS= \
	$$(ios_CFLAGS) \
	-isysroot $$(XCODE_DIR)/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator$$(IOS_VERSION).sdk -mios-simulator-version-min=$$(IOS_VERSION_MIN) \
	-arch $(2) \
	-Wl,-application_extension \
	-DHOST_IOS \
	$$(ios_$(1)_CFLAGS)

_ios_$(1)_CPPFLAGS= \
	$$(ios_CPPFLAGS) \
	-isysroot $$(XCODE_DIR)/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator$$(IOS_VERSION).sdk -mios-simulator-version-min=$$(IOS_VERSION_MIN) \
	-arch $(2) \
	-Wl,-application_extension \
	-DHOST_IOS \
	$$(ios_$(1)_CPPFLAGS)

_ios_$(1)_CXXFLAGS= \
	$$(ios_CXXFLAGS) \
	-isysroot $$(XCODE_DIR)/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator$$(IOS_VERSION).sdk -mios-simulator-version-min=$$(IOS_VERSION_MIN) \
	-arch $(2) \
	-Wl,-application_extension\
	-DHOST_IOS \
	$$(ios_$(1)_CXXFLAGS)

_ios_$(1)_LDFLAGS= \
	$$(ios_LDFLAGS) \
	$$(ios_$(1)_LDFLAGS)

_ios_$(1)_CONFIGURE_ENVIRONMENT = \
	CC="$$(_ios_$(1)_CC)" \
	CXX="$$(_ios_$(1)_CXX)" \
	CFLAGS="$$(_ios_$(1)_CFLAGS)" \
	CPPFLAGS="$$(_ios_$(1)_CPPFLAGS)" \
	CXXFLAGS="$$(_ios_$(1)_CXXFLAGS)" \
	LDFLAGS="$$(_ios_$(1)_LDFLAGS)"

_ios_$(1)_CONFIGURE_FLAGS= \
	--host=$(2)-apple-darwin10 \
	--cache-file=$$(TOP)/sdks/builds/ios-$(1).config.cache \
	--prefix=$$(TOP)/sdks/out/ios-$(1) \
	--disable-boehm \
	--disable-btls \
	--disable-executables \
	--disable-iconv \
	--disable-mcs-build \
	--disable-nls \
	--disable-visibility-hidden \
	--enable-maintainer-mode \
	--enable-minimal=com,remoting,shared_perfcounters \
	--with-tls=pthread \
	--without-ikvm-native

# _ios_$(1)_CONFIGURE_FLAGS += --enable-extension-module=xamarin

.stamp-ios-$(1)-toolchain:
	touch $$@

.stamp-ios-$(1)-configure: $$(TOP)/configure
	mkdir -p $$(TOP)/sdks/builds/ios-$(1)
	cd $$(TOP)/sdks/builds/ios-$(1) && PATH="$$(PLATFORM_BIN):$$$$PATH" $$(TOP)/configure $$(_ios_$(1)_AC_VARS) $$(_ios_$(1)_CONFIGURE_ENVIRONMENT) $$(_ios_$(1)_CONFIGURE_FLAGS)
	touch $$@

.PHONY: package-ios-$(1)
package-ios-$(1):
	$(MAKE) -C $$(TOP)/sdks/builds/ios-$(1)/mono install
	$(MAKE) -C $$(TOP)/sdks/builds/ios-$(1)/support install

.PHONY: clean-ios-$(1)
clean-ios-$(1):
	rm -rf .stamp-ios-$(1)-toolchain .stamp-ios-$(1)-configure $$(TOP)/sdks/builds/ios-$(1) $$(TOP)/sdks/builds/ios-$(1).config.cache $$(TOP)/sdks/out/ios-$(1)

TARGETS += ios-$(1)

endef

$(eval $(call iOSSimulatorTemplate,sim32,i386))
$(eval $(call iOSSimulatorTemplate,sim64,x86_64))

$(TOP)/tools/offsets-tool/MonoAotOffsetsDumper.exe: $(wildcard $(TOP)/tools/offsets-tool/*.cs)
	$(MAKE) -C $(dir $@) MonoAotOffsetsDumper.exe

LLVM_REV=3b82b3c9041eb997f627f881a67d20be37264e9c

# Download a prebuilt llvm
.stamp-ios-llvm-$(LLVM_REV):
	./download-llvm.sh $(LLVM_REV)
	touch $@

build-ios-llvm: .stamp-ios-llvm-$(LLVM_REV)

clean-ios-llvm:
	$(RM) -rf ../out/ios-llvm64 ../out/ios-llvm32 .stamp-ios-llvm-$(LLVM_REV)

##
# Parameters:
#  $(1): target (cross32 or cross64)
#  $(2): arch (arm or aarch64)
#  $(3): llvm (llvm32 or llvm64)
#
# Flags:
#  ios_$(1)_AC_VARS
#  ios_$(1)_CFLAGS
#  ios_$(1)_CXXFLAGS
#  ios_$(1)_LDFLAGS
#  ios_$(1)_CONFIGURE_FLAGS
define iOSCrossTemplate

_ios_$(1)_CC=$$(CCACHE) $$(PLATFORM_BIN)/clang
_ios_$(1)_CXX=$$(CCACHE) $$(PLATFORM_BIN)/clang++

_ios_$(1)_AC_VARS= \
	$$(ios_$(1)_AC_VARS)

_ios_$(1)_CFLAGS= \
	$$(ios_CFLAGS) \
	-isysroot $$(XCODE_DIR)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX$$(MACOS_VERSION).sdk -mmacosx-version-min=$$(MACOS_VERSION_MIN) \
	-Qunused-arguments \
	$$(ios_$(1)_CFLAGS)

_ios_$(1)_CXXFLAGS= \
	$$(ios_CXXFLAGS) \
	-isysroot $$(XCODE_DIR)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX$$(MACOS_VERSION).sdk -mmacosx-version-min=$$(MACOS_VERSION_MIN) \
	-Qunused-arguments \
	-stdlib=libc++ \
	$$(ios_$(1)_CXXFLAGS)

_ios_$(1)_LDFLAGS= \
	$$(ios_LDFLAGS) \
	-stdlib=libc++ \
	$$(ios_$(1)_LDFLAGS)

_ios_$(1)_CONFIGURE_FLAGS= \
	$$(ios_$(1)_CONFIGURE_FLAGS) \
	--target=$(2)-darwin \
	--cache-file=$$(TOP)/sdks/builds/ios-$(1).config.cache \
	--prefix=$$(TOP)/sdks/out/ios-$(1) \
	--disable-boehm \
	--disable-btls \
	--disable-iconv \
	--disable-libraries \
	--disable-mcs-build \
	--disable-nls \
	--enable-dtrace=yes \
	--enable-icall-symbol-map \
	--enable-minimal=com,remoting \
	--with-cross-offsets=$(2)-apple-darwin10.h \
	--with-llvm=$$(TOP)/sdks/out/ios-$(3)

_ios_$(1)_CONFIGURE_ENVIRONMENT= \
	CC="$$(_ios_$(1)_CC)" \
	CXX="$$(_ios_$(1)_CXX)" \
	CFLAGS="$$(_ios_$(1)_CFLAGS)" \
	CXXFLAGS="$$(_ios_$(1)_CXXFLAGS)" \
	LDFLAGS="$$(_ios_$(1)_LDFLAGS)"

.stamp-ios-$(1)-toolchain:
	touch $$@

.stamp-ios-$(1)-configure: $$(TOP)/configure | build-ios-llvm
	mkdir -p $$(TOP)/sdks/builds/ios-$(1)
	cd $$(TOP)/sdks/builds/ios-$(1) && PATH="$$(PLATFORM_BIN):$$$$PATH" $$(TOP)/configure $$(_ios_$(1)_AC_VARS) $$(_ios_$(1)_CONFIGURE_ENVIRONMENT) $$(_ios_$(1)_CONFIGURE_FLAGS)
	touch $$@

$$(TOP)/sdks/builds/ios-$(1)/mono/utils/mono-dtrace.h: .stamp-ios-$(1)-configure
	$$(MAKE) -C $$(dir $$@) $$(notdir $$@)

$$(TOP)/sdks/builds/ios-$(1)/$(2)-apple-darwin10.h: .stamp-ios-$(1)-configure $$(TOP)/sdks/builds/ios-$(1)/mono/utils/mono-dtrace.h $$(TOP)/tools/offsets-tool/MonoAotOffsetsDumper.exe
	cd $$(TOP)/sdks/builds/ios-$(1) && \
		MONO_PATH=$(TOP)/tools/offsets-tool/CppSharp/osx_32 \
			mono --arch=32 --debug $$(TOP)/tools/offsets-tool/MonoAotOffsetsDumper.exe \
				--gen-ios --abi $(2)-apple-darwin10 --out $$(TOP)/sdks/builds/ios-$(1)/ --mono $$(TOP) --targetdir $$(TOP)/sdks/builds/ios-$(1)

build-ios-$(1): $$(TOP)/sdks/builds/ios-$(1)/$(2)-apple-darwin10.h

.PHONY: package-ios-$(1)
package-ios-$(1):
	$$(MAKE) -C $$(TOP)/sdks/builds/ios-$(1)/mono install

.PHONY: clean-ios-$(1)
clean-ios-$(1):
	rm -rf .stamp-ios-$(1)-toolchain .stamp-ios-$(1)-configure $$(TOP)/sdks/builds/ios-$(1) $$(TOP)/sdks/builds/ios-$(1).config.cache $$(TOP)/sdks/out/ios-$(1)

TARGETS += ios-$(1)

endef

ios_cross32_CONFIGURE_FLAGS=--build=i386-apple-darwin10
$(eval $(call iOSCrossTemplate,cross32,arm,llvm32))
$(eval $(call iOSCrossTemplate,cross64,aarch64,llvm64))


