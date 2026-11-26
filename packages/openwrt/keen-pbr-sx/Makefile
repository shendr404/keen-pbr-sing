include $(TOPDIR)/rules.mk

KEEN_PBR_PACKAGE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
KEEN_PBR_SRC ?= $(abspath $(KEEN_PBR_PACKAGE_DIR)/../../..)
KEEN_PBR_VERSION_FILE := $(firstword $(wildcard \
	$(KEEN_PBR_PACKAGE_DIR)/version.mk \
	$(KEEN_PBR_SRC)/version.mk \
	$(KEEN_PBR_PACKAGE_DIR)/../../../version.mk))
include $(KEEN_PBR_VERSION_FILE)
PKG_NAME:=keen-pbr-sx
PKG_VERSION:=$(KEEN_PBR_VERSION)
PKG_RELEASE:=$(KEEN_PBR_RELEASE)
PKG_MAINTAINER:=shendr404 <143122797+shendr404@users.noreply.github.com>

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

PKG_BUILD_DIR=$(BUILD_DIR)/$(PKG_NAME)$(if $(BUILD_VARIANT),-$(BUILD_VARIANT))/$(PKG_NAME)-$(PKG_VERSION)
CMAKE_BINARY_SUBDIR:=cmake-build

KEEN_PBR_FRONTEND_DIST ?= $(KEEN_PBR_SRC)/frontend/dist
KEEN_PBR_FULL_CONFIG ?= $(PKG_BUILD_DIR)/packages/common/config.full.example.json
KEEN_PBR_HEADLESS_CONFIG ?= $(PKG_BUILD_DIR)/packages/common/config.headless.example.json
KEEN_PBR_CMAKE_INSTALL_BINDIR ?= sbin
KEEN_PBR_STAGED_BINDIR ?= usr/$(KEEN_PBR_CMAKE_INSTALL_BINDIR)
KEEN_PBR_OBJCOPY ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)objcopy,objcopy)
KEEN_PBR_READELF ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)readelf,readelf)
KEEN_PBR_NM ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)nm,nm)
KEEN_PBR_ADDR2LINE ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)addr2line,addr2line)
KEEN_PBR_DEBUG_ARTIFACT_DIR ?= $(BUILD_DIR)/debug-artifacts/$(PKG_NAME)$(if $(BUILD_VARIANT),-$(BUILD_VARIANT))/$(BUILD_VARIANT)
KEEN_PBR_STAGED_BIN ?= $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr-sx

define KeenPbr/EmitDebugSymbol
	@if [ -f "$(KEEN_PBR_STAGED_BIN)" ] && command -v "$(KEEN_PBR_OBJCOPY)" >/dev/null 2>&1; then \
		mkdir -p "$(KEEN_PBR_DEBUG_ARTIFACT_DIR)"; \
		"$(KEEN_PBR_OBJCOPY)" --only-keep-debug --compress-debug-sections=zlib \
			"$(KEEN_PBR_STAGED_BIN)" \
			"$(KEEN_PBR_DEBUG_ARTIFACT_DIR)/keen-pbr-sx.debug"; \
		cd "$(KEEN_PBR_DEBUG_ARTIFACT_DIR)" && \
			"$(KEEN_PBR_OBJCOPY)" --strip-debug --add-gnu-debuglink=keen-pbr-sx.debug \
			"$(1)"; \
		sh "$(PKG_BUILD_DIR)/build_scripts/verify-debug-artifact.sh" \
			"$(1)" \
			"$(KEEN_PBR_DEBUG_ARTIFACT_DIR)/keen-pbr-sx.debug" \
			"$(KEEN_PBR_READELF)" "$(KEEN_PBR_NM)" \
			"$(KEEN_PBR_ADDR2LINE)" "$(KEEN_PBR_OBJCOPY)"; \
	fi
endef

#
# Common blocks
#

# Copy every helper under files/usr/lib/keen-pbr-sx. INSTALL_BIN is not used
# for that directory, so a new file cannot be left out of the package.
# *.sh is forced executable: firewall.sh is not executable in git.
define KeenPbr/InstallOpenWrtLib
	$(INSTALL_DIR) $(1)/usr/lib/keen-pbr-sx
	$(CP) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/usr/lib/keen-pbr-sx/. $(1)/usr/lib/keen-pbr-sx/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/common/cleanup-firewall.sh $(1)/usr/lib/keen-pbr-sx/cleanup-firewall.sh
	chmod 755 $(1)/usr/lib/keen-pbr-sx/*.sh
endef

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	rsync -a --delete \
		--exclude='frontend/node_modules' \
		--exclude='frontend/dist' \
		$(KEEN_PBR_SRC)/ $(PKG_BUILD_DIR)/
ifneq ($(BUILD_VARIANT),headless)
	mkdir -p $(PKG_BUILD_DIR)/frontend
	cp -a $(KEEN_PBR_FRONTEND_DIST) $(PKG_BUILD_DIR)/frontend/dist
	@test -d $(PKG_BUILD_DIR)/frontend/dist || (echo "Missing frontend/dist (provide frontend-build artifacts in KEEN_PBR_FRONTEND_DIST)"; false)
endif
endef

ifeq ($(CONFIG_x86_64)$(CONFIG_aarch64)$(CONFIG_arm),)
  KEEN_PBR_ENABLE_LIBUNWIND:=OFF
else
  KEEN_PBR_ENABLE_LIBUNWIND:=$(if $(CONFIG_PACKAGE_libunwind),ON,OFF)
endif

CMAKE_OPTIONS += \
	-DKEEN_PBR_DEFAULT_CONFIG_PATH:STRING=/etc/keen-pbr-sx/config.json \
	-DKEEN_PBR_CONTROL_SOCKET:STRING=/var/run/keen-pbr-sx/control.sock \
	-DKEEN_PBR_DNSMASQ_HOOK:STRING=/usr/lib/keen-pbr-sx/dnsmasq-hook.sh \
	-DKEEN_PBR_RESOLVER_FALLBACK_CONFIG:STRING=/etc/keen-pbr-sx/dnsmasq-fallback.conf \
	-DKEEN_PBR_CLEANUP_FIREWALL_SCRIPT:STRING=/usr/lib/keen-pbr-sx/cleanup-firewall.sh \
	-DKEEN_PBR_TARGET_OS:STRING=openwrt \
	-DKEEN_PBR_TARGET_VERSION:STRING=$(KEEN_PBR_TARGET_VERSION) \
	-DKEEN_PBR_TARGET_ARCH:STRING=$(KEEN_PBR_TARGET_ARCH) \
	-DKEEN_PBR_BUILD_VARIANT:STRING=$(if $(BUILD_VARIANT),$(BUILD_VARIANT),full) \
	-DKEEN_PBR_GIT_COMMIT:STRING=$(KEEN_PBR_GIT_COMMIT) \
	-DKEEN_PBR_GIT_BRANCH:STRING=$(KEEN_PBR_GIT_BRANCH) \
	-DCMAKE_INSTALL_BINDIR:PATH=$(KEEN_PBR_CMAKE_INSTALL_BINDIR) \
	-DKEEN_PBR_USE_LIBUNWIND=$(KEEN_PBR_ENABLE_LIBUNWIND) \
	-DCMAKE_BUILD_TYPE=MinSizeRel

ifneq ($(BUILD_VARIANT),headless)
  CMAKE_OPTIONS += \
	-DKEEN_PBR_FRONTEND_ROOT:STRING=/usr/share/keen-pbr-sx/frontend \
	-DWITH_API=ON
else
  CMAKE_OPTIONS += \
	-DWITH_API=OFF
endif

#
# keen-pbr-sx blocks
#

define Package/keen-pbr-sx
  SECTION:=net
  CATEGORY:=Network
  URL:=https://github.com/shendr404/keen-pbr-sx
  MAINTAINER:=shendr404 <143122797+shendr404@users.noreply.github.com>
  TITLE:=keen-pbr-sx (full)
  VERSION:=$(if $(CONFIG_USE_APK),$(PKG_VERSION)-r$(PKG_RELEASE),$(PKG_VERSION)-$(PKG_RELEASE))
  DEPENDS:=+conntrack +kmod-nfnetlink-queue +kmod-nfnetlink-log +kmod-nft-queue +kmod-nf-conntrack-netlink +libatomic +libcurl +libnl-core +libnl-route +libstdcpp +(x86_64||aarch64||arm):libunwind +zlib +libzstd
  CONFLICTS:=keen-pbr
  PROVIDES:=keen-pbr
  REPLACES:=keen-pbr
  VARIANT:=full
endef

define Package/keen-pbr-sx/description
  keen-pbr-sx: sing-box TUN routing, with per-outbound traffic stats, REST API and WebUI. Replaces the keen-pbr package. Based on keen-pbr by Maxim Kurbatov.
endef

define Package/keen-pbr-sx/conffiles
/etc/keen-pbr-sx/config.json
/etc/keen-pbr-sx/dnsmasq-fallback.conf
/etc/keen-pbr-sx/local.lst
endef

define Package/keen-pbr-sx/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/hotplug.d/firewall
	$(INSTALL_DIR) $(1)/etc/keen-pbr-sx
	$(INSTALL_DIR) $(1)/usr/lib/keen-pbr-sx
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_DIR) $(1)/usr/share/keen-pbr-sx/frontend
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr-sx $(1)/usr/sbin/keen-pbr-sx
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/etc/init.d/keen-pbr-sx $(1)/etc/init.d/keen-pbr-sx
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/etc/init.d/keen-pbr-sx-dnsmasq $(1)/etc/init.d/keen-pbr-sx-dnsmasq
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/etc/hotplug.d/firewall/70-keen-pbr-sx $(1)/etc/hotplug.d/firewall/70-keen-pbr-sx
	$(call KeenPbr/InstallOpenWrtLib,$(1))
	$(CP) $(PKG_BUILD_DIR)/frontend/dist/. $(1)/usr/share/keen-pbr-sx/frontend/
	chmod -R a=rX,u+w $(1)/usr/share/keen-pbr-sx/frontend
	$(INSTALL_CONF) $(KEEN_PBR_FULL_CONFIG) $(1)/etc/keen-pbr-sx/config.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/etc/keen-pbr-sx/dnsmasq-fallback.conf $(1)/etc/keen-pbr-sx/dnsmasq-fallback.conf
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/etc/keen-pbr-sx/local.lst
	$(call KeenPbr/EmitDebugSymbol,$(1)/usr/sbin/keen-pbr-sx)
endef

define Package/keen-pbr-sx/postinst
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/postinst)
endef

define Package/keen-pbr-sx/prerm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/prerm)
endef

#
# keen-pbr-sx-headless blocks
#

define Package/keen-pbr-sx-headless
  SECTION:=net
  CATEGORY:=Network
  URL:=https://github.com/shendr404/keen-pbr-sx
  MAINTAINER:=shendr404 <143122797+shendr404@users.noreply.github.com>
  TITLE:=keen-pbr-sx (headless)
  VERSION:=$(if $(CONFIG_USE_APK),$(PKG_VERSION)-r$(PKG_RELEASE),$(PKG_VERSION)-$(PKG_RELEASE))
  DEPENDS:=+conntrack +kmod-nfnetlink-queue +kmod-nfnetlink-log +kmod-nft-queue +kmod-nf-conntrack-netlink +libatomic +libcurl +libnl-core +libnl-route +libstdcpp +(x86_64||aarch64||arm):libunwind +zlib +libzstd
  CONFLICTS:=keen-pbr-headless
  PROVIDES:=keen-pbr-headless
  REPLACES:=keen-pbr-headless
  VARIANT:=headless
endef

define Package/keen-pbr-sx-headless/description
  keen-pbr-sx: sing-box TUN routing without the REST API or WebUI. Replaces keen-pbr-headless. Based on keen-pbr by Maxim Kurbatov.
endef

define Package/keen-pbr-sx-headless/conffiles
/etc/keen-pbr-sx/config.json
/etc/keen-pbr-sx/dnsmasq-fallback.conf
/etc/keen-pbr-sx/local.lst
endef

define Package/keen-pbr-sx-headless/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/hotplug.d/firewall
	$(INSTALL_DIR) $(1)/etc/keen-pbr-sx
	$(INSTALL_DIR) $(1)/usr/lib/keen-pbr-sx
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr-sx $(1)/usr/sbin/keen-pbr-sx
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/etc/init.d/keen-pbr-sx $(1)/etc/init.d/keen-pbr-sx
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/etc/init.d/keen-pbr-sx-dnsmasq $(1)/etc/init.d/keen-pbr-sx-dnsmasq
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/etc/hotplug.d/firewall/70-keen-pbr-sx $(1)/etc/hotplug.d/firewall/70-keen-pbr-sx
	$(call KeenPbr/InstallOpenWrtLib,$(1))
	$(INSTALL_CONF) $(KEEN_PBR_HEADLESS_CONFIG) $(1)/etc/keen-pbr-sx/config.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/etc/keen-pbr-sx/dnsmasq-fallback.conf $(1)/etc/keen-pbr-sx/dnsmasq-fallback.conf
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/etc/keen-pbr-sx/local.lst
	$(call KeenPbr/EmitDebugSymbol,$(1)/usr/sbin/keen-pbr-sx)
endef

define Package/keen-pbr-sx-headless/postinst
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/postinst)
endef

define Package/keen-pbr-sx-headless/prerm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr-sx/files/prerm)
endef

$(eval $(call BuildPackage,keen-pbr-sx))
$(eval $(call BuildPackage,keen-pbr-sx-headless))
