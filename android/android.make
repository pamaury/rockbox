#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
# $Id$
#

.SECONDEXPANSION: # $$(JAVA_OBJ) is not populated until after this
.SECONDEXPANSION: # $$(OBJ) is not populated until after this


$(BUILDDIR)/$(BINARY): $$(OBJ) $(VOICESPEEXLIB) $(FIRMLIB) $(SKINLIB)
	$(call PRINTS,LD $(BINARY))$(CC) -o $@ $^ $(LDOPTS) $(GLOBAL_LDOPTS)

PACKAGE=org.rockbox
PACKAGE_PATH=org/rockbox
ANDROID_DIR=$(ROOTDIR)/android


java2class = $(addsuffix .class,$(basename $(subst $(ANDROID_DIR),$(BUILDDIR),$(1))))

ANDROID_PLATFORM_VERSION=8

ANDROID_PLATFORM=$(ANDROID_SDK_PATH)/platforms/android-$(ANDROID_PLATFORM_VERSION)
AAPT=$(ANDROID_PLATFORM)/tools/aapt
DX=$(ANDROID_PLATFORM)/tools/dx
APKBUILDER=$(ANDROID_SDK_PATH)/tools/apkbuilder
ZIPALIGN=$(ANDROID_SDK_PATH)/tools/zipalign


MANIFEST	:= $(ANDROID_DIR)/AndroidManifest.xml

R_JAVA		:= $(BUILDDIR)/gen/$(PACKAGE_PATH)/R.java
R_OBJ		:= $(BUILDDIR)/bin/$(PACKAGE_PATH)/R.class

JAVA_SRC	:= $(wildcard $(ANDROID_DIR)/src/$(PACKAGE_PATH)/*.java)
JAVA_OBJ	:= $(call java2class,$(subst /src/,/bin/,$(JAVA_SRC)))

LIBS		:= $(BUILDDIR)/libs/armeabi/$(BINARY) $(BUILDDIR)/libs/armeabi/libmisc.so
TEMP_APK	:= $(BUILDDIR)/bin/_Rockbox.apk
APK			:= $(BUILDDIR)/bin/Rockbox.apk

$(R_JAVA): $(MANIFEST)
	$(call PRINTS,AAPT $(subst $(BUILDDIR)/,,$<))$(AAPT) package -f -m -J $(BUILDDIR)/gen -M $(MANIFEST) -S $(ANDROID_DIR)/res -I $(ANDROID_PLATFORM)/android.jar -F $(BUILDDIR)/bin/resources.ap_

$(BUILDDIR)/bin/$(PACKAGE_PATH)/R.class: $(R_JAVA)
	$(call PRINTS,JAVAC $(subst $(BUILDDIR)/,,$<))javac -d $(BUILDDIR)/bin \
	-classpath $(ANDROID_PLATFORM)/android.jar:$(BUILDDIR)/bin -sourcepath \
	$(ANDROID_DIR)/gen:$(ANDROID_DIR)/src $<

$(BUILDDIR)/bin/$(PACKAGE_PATH)/%.class: $(ANDROID_DIR)/src/$(PACKAGE_PATH)/%.java
	$(call PRINTS,JAVAC $(subst $(BUILDDIR)/,,$<))javac -d $(BUILDDIR)/bin \
	-classpath $(ANDROID_PLATFORM)/android.jar:$(BUILDDIR)/bin -sourcepath \
	$(ANDROID_DIR)/gen:$(ANDROID_DIR)/src $<

classes: $(R_OBJ) $(JAVA_OBJ)

$(BUILDDIR)/bin/classes.dex: classes
	$(call PRINTS,DX $(subst $(BUILDDIR)/,,$@))$(DX) --dex --output=$@ $(BUILDDIR)/bin

dex: $(BUILDDIR)/bin/classes.dex

$(BUILDDIR)/libs/armeabi/$(BINARY): $(BUILDDIR)/$(BINARY)
	$(call PRINTS,CP $(BINARY))cp $^ $@

$(BUILDDIR)/_rockbox.zip: zip
	$(SILENT)mv $(BUILDDIR)/rockbox.zip $@

$(BUILDDIR)/libs/armeabi/libmisc.so: $(BUILDDIR)/_rockbox.zip
	$(call PRINTS,CP rockbox.zip)cp $^ $@

libs: $(LIBS)

$(TEMP_APK): libs dex
	$(call PRINTS,APK $(subst $(BUILDDIR)/,,$@))$(APKBUILDER) $@ \
	-u -z $(BUILDDIR)/bin/resources.ap_ -f $(BUILDDIR)/bin/classes.dex -nf $(BUILDDIR)/libs

$(APK): $(TEMP_APK)
	$(SILENT)rm -f $@
	$(call PRINTS,SIGN $(subst $(BUILDDIR)/,,$@))jarsigner \
	-keystore "$(HOME)/.android/debug.keystore" -storepass "android" \
	-keypass "android" -signedjar bin/__Rockbox.apk $^ "androiddebugkey"
	$(SILENT)$(ZIPALIGN) -v 4 bin/__Rockbox.apk $@ > /dev/null

apk: $(APK)