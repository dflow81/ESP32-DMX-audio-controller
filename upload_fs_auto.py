Import("env")

def after_upload(source, target, env):
    print("=== Uploading LittleFS filesystem ===")
    # Upload the file system only for the environment in use (USB or OTA)
    env.Execute("pio run -e %s -t uploadfs" % env["PIOENV"])

env.AddPostAction("upload", after_upload)
