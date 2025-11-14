lib_opencv_inc_dir := $(SDK_RTSMART_SRC_DIR)/libs/opencv/include/opencv4

lib_opencv_lib_dir := $(SDK_RTSMART_SRC_DIR)/libs/opencv/lib
lib_opencv_lib_dir += $(SDK_RTSMART_SRC_DIR)/libs/opencv/lib/opencv4/3rdparty

LIB_CFLAGS += $(addprefix -I, $(lib_opencv_inc_dir))
LIB_LDFLAGS += $(addprefix -L, $(lib_opencv_lib_dir)) 
LIB_LDFLAGS += -Wl,--start-group -lstdc++ -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lopencv_calib3d -lopencv_features2d -lopencv_flann -lzlib -llibjpeg-turbo -llibopenjp2 -llibpng -llibtiff -llibwebp -lcsi_cv -latomic -Wl,--end-group
