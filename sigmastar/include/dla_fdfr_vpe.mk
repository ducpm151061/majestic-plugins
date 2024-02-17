INC  += $(DB_BUILD_TOP)/internal/live555/UsageEnvironment/include
INC  += $(DB_BUILD_TOP)/internal/live555/groupsock/include
INC  += $(DB_BUILD_TOP)/internal/live555/liveMedia/include
INC  += $(DB_BUILD_TOP)/internal/live555/BasicUsageEnvironment/include
INC  += $(DB_BUILD_TOP)/internal/live555/mediaServer/include
INC  += $(DB_BUILD_TOP)/internal/iniparser
INC  += ./internal/ldc
INC  += $(DB_BUILD_TOP)/internal/opencv/include/opencv4
INC  += $(DB_BUILD_TOP)/internal/tem
#ST_DEP := common  iniparser
LIBS += -lmi_ipu -lcam_fs_wrapper

#static library path
LIBS += -L$(DB_BUILD_TOP)/internal/opencv/static_lib_${TOOLCHAIN_VERSION} -L$(DB_BUILD_TOP)/internal/opencv/static_lib_${TOOLCHAIN_VERSION}/opencv4/3rdparty
#shared library path
#LIBS += -L$(DB_BUILD_TOP)/internal/opencv/shared_lib_${TOOLCHAIN_VERSION}
#shared opencv library
#LIBS += -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
#static library
LIBS += -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -littnotify -llibjasper  -llibjpeg-turbo  -llibpng -llibtiff  -llibwebp -ltegra_hal -lzlib ./dla_fdfr_vpe/libsigma_model.a

ST_DEP := common vpe venc vif  live555 iniparser

LIBS += -L./internal/ldc
LIBS += -lmi_sensor -lmi_vif -lmi_vpe -lmi_venc -lmi_divp -lmi_rgn -lmi_iqserver
LIBS += -lmi_isp -lcus3a -lispalgo
ifeq ($(CHIP), i6e)
LIBS +=-lfbc_decode -leptz
endif
