Import("env")

import subprocess

config = env.GetProjectConfig()

# get the env name for this build and check if we are building for 8266
# set the required firmware name (ESP8622  or  ESP32)
env_name = env.subst("$PIOENV")
if "8266" in env_name:
  firmware_name_raw = config.get("firmware", "name_esp8266")
else:
  firmware_name_raw = config.get("firmware", "name_esp32")

firmware_name = firmware_name_raw.replace('\\\"', '')
print("Firmware Name: %s" % firmware_name)

# query the current version via git tags (unannotated)
ret = subprocess.run(["git", "describe", "--tags"], stdout=subprocess.PIPE, text=True)
firmware_version = ret.stdout.strip()

print("Firmware Name: %s" % firmware_name)
print("Firmware Version: %s" % firmware_version)

env.Append(
    BUILD_FLAGS=["-DFW_VERSION=%s" % (firmware_version)]
)
env.Append(
    BUILD_FLAGS=["-DFW_NAME=%s" % (firmware_name_raw)]
)

env.Replace(
    PROGNAME="%s_%s_v%s" % (firmware_name, env_name, firmware_version)
)
