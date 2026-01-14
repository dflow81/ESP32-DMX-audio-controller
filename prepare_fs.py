Import("env")
import os

data_dir = os.path.join(env["PROJECT_DIR"], "data")

if not os.path.exists(data_dir):
    print("Creating /data folder...")
    os.makedirs(data_dir)
