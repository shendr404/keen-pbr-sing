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
KEEN_PBR_FULL_CONFIG ?= $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/keen-pbr-sx/config.full.example.json
KEEN_PBR_HEADLESS_CONFIG ?= $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/keen-pbr-sx/config.headless.example.json
KEEN_PBR_CMAKE_INSTALL_BINDIR ?= usr/bin
KEEN_PBR_STAGED_BINDIR ?= opt/$(KEEN_PBR_CMAKE_INSTALL_BINDIR)
KEEN_PBR_OBJCOPY ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)objcopy,objcopy)
KEEN_PBR_READELF ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)readelf,readelf)
KEEN_PBR_NM ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)nm,nm)
KEEN_PBR_ADDR2LINE ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)addr2line,addr2line)
KEEN_PBR_DEBUG_ARTIFACT_DIR ?= $(PKG_BUILD_DIR)/debug-artifacts/$(BUILD_VARIANT)
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

CMAKE_OPTIONS += \
	-DKEEN_PBR_DEFAULT_CONFIG_PATH:STRING=/opt/etc/keen-pbr-sx/config.json \
	-DKEEN_PBR_CONTROL_SOCKET:STRING=/tmp/keen-pbr-sx/control.sock \
	-DKEEN_PBR_DNSMASQ_HOOK:STRING=/opt/usr/lib/keen-pbr-sx/dnsmasq-hook.sh \
	-DKEEN_PBR_RESOLVER_FALLBACK_CONFIG:STRING=/opt/etc/keen-pbr-sx/dnsmasq-fallback.conf \
	-DKEEN_PBR_CLEANUP_FIREWALL_SCRIPT:STRING=/opt/usr/lib/keen-pbr-sx/cleanup-firewall.sh \
	-DKEEN_PBR_TARGET_OS:STRING=keenetic \
	-DKEEN_PBR_TARGET_VERSION:STRING=$(KEEN_PBR_TARGET_VERSION) \
	-DKEEN_PBR_TARGET_ARCH:STRING=$(KEEN_PBR_TARGET_ARCH) \
	-DKEEN_PBR_BUILD_VARIANT:STRING=$(if $(BUILD_VARIANT),$(BUILD_VARIANT),full) \
	-DKEEN_PBR_GIT_COMMIT:STRING=$(KEEN_PBR_GIT_COMMIT) \
	-DKEEN_PBR_GIT_BRANCH:STRING=$(KEEN_PBR_GIT_BRANCH) \
	-DCMAKE_INSTALL_BINDIR:PATH=$(KEEN_PBR_CMAKE_INSTALL_BINDIR) \
	-DUSE_KEENETIC_API=ON \
	-DKEEN_PBR_PLATFORM_KEENETIC=ON \
	-DCMAKE_BUILD_TYPE=MinSizeRel

ifneq ($(BUILD_VARIANT),headless)
  CMAKE_OPTIONS += \
	-DKEEN_PBR_FRONTEND_ROOT:STRING=/opt/usr/share/keen-pbr-sx/frontend \
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
  VERSION:=$(PKG_VERSION)-$(PKG_RELEASE)
  DEPENDS:=+conntrack +ipset +iptables +libatomic +libcurl +libnl-core +libnl-route +libstdcpp +zlib +libzstd
  CONFLICTS:=keen-pbr
  PROVIDES:=keen-pbr
  REPLACES:=keen-pbr
  VARIANT:=full
endef

define Package/keen-pbr-sx/description
  keen-pbr-sx: sing-box TUN routing for Keenetic, with per-outbound traffic stats, REST API and WebUI. Replaces the keen-pbr package. Based on keen-pbr by Maxim Kurbatov.
endef

define Package/keen-pbr-sx/conffiles
/opt/etc/keen-pbr-sx/config.json
/opt/etc/keen-pbr-sx/defaults
/opt/etc/keen-pbr-sx/local.lst
/opt/etc/keen-pbr-sx/dnsmasq-fallback.conf
endef

define Package/keen-pbr-sx/install
	$(INSTALL_DIR) $(1)/opt/etc/init.d
	$(INSTALL_DIR) $(1)/opt/etc/keen-pbr-sx
	$(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d
	$(INSTALL_DIR) $(1)/opt/usr/bin
	$(INSTALL_DIR) $(1)/opt/usr/lib/keen-pbr-sx
	$(INSTALL_DIR) $(1)/opt/usr/share/keen-pbr-sx/frontend
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr-sx $(1)/opt/usr/bin/keen-pbr-sx
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/init.d/S80keen-pbr-sx $(1)/opt/etc/init.d/S80keen-pbr-sx
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/keen-pbr-sx/defaults $(1)/opt/etc/keen-pbr-sx/defaults
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/keen-pbr-sx/dnsmasq-fallback.conf $(1)/opt/etc/keen-pbr-sx/dnsmasq-fallback.conf
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/dnsmasq.sh $(1)/opt/usr/lib/keen-pbr-sx/dnsmasq.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/dnsmasq-hook.sh $(1)/opt/usr/lib/keen-pbr-sx/dnsmasq-hook.sh
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/dnsmasq.recommended.conf $(1)/opt/usr/lib/keen-pbr-sx/dnsmasq.recommended.conf
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/common/cleanup-firewall.sh $(1)/opt/usr/lib/keen-pbr-sx/cleanup-firewall.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/ndm/netfilter.d/50-keen-pbr-sx-routing.sh $(1)/opt/etc/ndm/netfilter.d/50-keen-pbr-sx-routing.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/ndm/netfilter.d/51-keen-pbr-sx-forward.sh $(1)/opt/etc/ndm/netfilter.d/51-keen-pbr-sx-forward.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/interface-forward.sh $(1)/opt/usr/lib/keen-pbr-sx/interface-forward.sh
	$(CP) $(PKG_BUILD_DIR)/frontend/dist/. $(1)/opt/usr/share/keen-pbr-sx/frontend/
	chmod -R a=rX,u+w $(1)/opt/usr/share/keen-pbr-sx/frontend
	$(INSTALL_CONF) $(KEEN_PBR_FULL_CONFIG) $(1)/opt/etc/keen-pbr-sx/config.json
	$(INSTALL_CONF) $(KEEN_PBR_FULL_CONFIG) $(1)/opt/etc/keen-pbr-sx/config.example.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/opt/etc/keen-pbr-sx/local.lst
	$(call KeenPbr/EmitDebugSymbol,$(1)/opt/usr/bin/keen-pbr-sx)
endef

define Package/keen-pbr-sx/postinst
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/postinst)
endef

define Package/keen-pbr-sx/prerm
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/prerm)
endef

define Package/keen-pbr-sx/postrm
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/postrm)
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
  VERSION:=$(PKG_VERSION)-$(PKG_RELEASE)
  DEPENDS:=+conntrack +ipset +iptables +libatomic +libcurl +libnl-core +libnl-route +libstdcpp +zlib +libzstd
  CONFLICTS:=keen-pbr-headless
  PROVIDES:=keen-pbr-headless
  REPLACES:=keen-pbr-headless
  VARIANT:=headless
endef

define Package/keen-pbr-sx-headless/description
  keen-pbr-sx: sing-box TUN routing for Keenetic, without the REST API or WebUI. Replaces keen-pbr-headless. Based on keen-pbr by Maxim Kurbatov.
endef

define Package/keen-pbr-sx-headless/conffiles
/opt/etc/keen-pbr-sx/config.json
/opt/etc/keen-pbr-sx/defaults
/opt/etc/keen-pbr-sx/local.lst
/opt/etc/keen-pbr-sx/dnsmasq-fallback.conf
endef

define Package/keen-pbr-sx-headless/install
	$(INSTALL_DIR) $(1)/opt/etc/init.d
	$(INSTALL_DIR) $(1)/opt/etc/keen-pbr-sx
	$(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d
	$(INSTALL_DIR) $(1)/opt/usr/bin
	$(INSTALL_DIR) $(1)/opt/usr/lib/keen-pbr-sx
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr-sx $(1)/opt/usr/bin/keen-pbr-sx
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/init.d/S80keen-pbr-sx $(1)/opt/etc/init.d/S80keen-pbr-sx
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/keen-pbr-sx/defaults $(1)/opt/etc/keen-pbr-sx/defaults
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/keen-pbr-sx/dnsmasq-fallback.conf $(1)/opt/etc/keen-pbr-sx/dnsmasq-fallback.conf
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/dnsmasq.sh $(1)/opt/usr/lib/keen-pbr-sx/dnsmasq.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/dnsmasq-hook.sh $(1)/opt/usr/lib/keen-pbr-sx/dnsmasq-hook.sh
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/dnsmasq.recommended.conf $(1)/opt/usr/lib/keen-pbr-sx/dnsmasq.recommended.conf
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/common/cleanup-firewall.sh $(1)/opt/usr/lib/keen-pbr-sx/cleanup-firewall.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/ndm/netfilter.d/50-keen-pbr-sx-routing.sh $(1)/opt/etc/ndm/netfilter.d/50-keen-pbr-sx-routing.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/etc/ndm/netfilter.d/51-keen-pbr-sx-forward.sh $(1)/opt/etc/ndm/netfilter.d/51-keen-pbr-sx-forward.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/opt/usr/lib/keen-pbr-sx/interface-forward.sh $(1)/opt/usr/lib/keen-pbr-sx/interface-forward.sh
	$(INSTALL_CONF) $(KEEN_PBR_HEADLESS_CONFIG) $(1)/opt/etc/keen-pbr-sx/config.json
	$(INSTALL_CONF) $(KEEN_PBR_HEADLESS_CONFIG) $(1)/opt/etc/keen-pbr-sx/config.example.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/opt/etc/keen-pbr-sx/local.lst
	$(call KeenPbr/EmitDebugSymbol,$(1)/opt/usr/bin/keen-pbr-sx)
endef

define Package/keen-pbr-sx-headless/postinst
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/postinst-headless)
endef

define Package/keen-pbr-sx-headless/prerm
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/prerm)
endef

define Package/keen-pbr-sx-headless/postrm
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr-sx/files/postrm)
endef

$(eval $(call BuildPackage,keen-pbr-sx))
$(eval $(call BuildPackage,keen-pbr-sx-headless))
