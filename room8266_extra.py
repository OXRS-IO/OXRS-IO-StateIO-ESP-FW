Import("env")
import os

platform = env.PioPlatform()

if (os.name == 'nt'):
  env.AddPostAction(
      "$BUILD_DIR\${PROGNAME}.bin",
      "copy $BUILD_DIR\${PROGNAME}.bin $BUILD_DIR\${PROGNAME_RAW}_FLASH.bin" 
  )
else:
  env.AddPostAction(
      "$BUILD_DIR/${PROGNAME}.bin",
      "cp $BUILD_DIR/${PROGNAME}.bin $BUILD_DIR/${PROGNAME_RAW}_FLASH.bin" 
  )

