Import("env")

import subprocess

config = env.GetProjectConfig()

# get the env name for this build and check if we are building for 8266
env_name = env.subst("$PIOENV")
if "8266" in env_name:
  firmware_name_raw = config.get("firmware", "name_esp8266")
else:
  firmware_name_raw = config.get("firmware", "name_esp32")

firmware_name = firmware_name_raw.replace('\\\"', '')
print("Firmware Name: %s" % firmware_name)

env.Append(
    BUILD_FLAGS=["-DFW_NAME=%s" % (firmware_name_raw)]
)
