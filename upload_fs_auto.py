Import("env")

def after_upload(source, target, env):
    print("=== Uploading LittleFS filesystem ===")
    # Nur für die gerade benutzte Umgebung (USB oder OTA) das Dateisystem hochladen
    env.Execute("pio run -e %s -t uploadfs" % env["PIOENV"])

env.AddPostAction("upload", after_upload)
