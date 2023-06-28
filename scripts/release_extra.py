Import("env")

import subprocess

config = env.GetProjectConfig()

# get the env name for this build 
env_name = env.subst("$PIOENV")
firmware_name = config.get("firmware", "name")
firmware_name = firmware_name.replace('\\\"', '')
print("Firmware Name: %s" % firmware_name)

# query the current version via git tags (unannotated)
ret = subprocess.run(["git", "describe", "--tags"], stdout=subprocess.PIPE, text=True)
firmware_version = ret.stdout.strip()

print("Firmware Name: %s" % firmware_name)
print("Firmware Version: %s" % firmware_version)

env.Append(
    BUILD_FLAGS=["-DFW_VERSION=%s" % (firmware_version)]
)

env.Replace(
    PROGNAME_RAW="%s_%s_v%s" % (firmware_name, env_name, firmware_version),
    PROGNAME="%s_%s_v%s_OTA" % (firmware_name, env_name, firmware_version)
)
