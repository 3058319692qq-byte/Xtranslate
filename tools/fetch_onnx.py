"""Fetch ONNX Runtime 1.27.1 from NuGet and arrange into third_party/onnxruntime/{include,lib}.

The .nupkg is a plain zip. We extract:
  - build/native/include/**            -> third_party/onnxruntime/include/**
  - runtimes/win-x64/native/onnxruntime.dll / .lib -> third_party/onnxruntime/lib/
"""
import os
import shutil
import sys
import urllib.request
import zipfile

URL = ("https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/"
       "1.27.1/microsoft.ml.onnxruntime.1.27.1.nupkg")
ROOT = r"e:\Transform\XTranslate\third_party\onnxruntime"
NUPKG = os.path.join(ROOT, "microsoft.ml.onnxruntime.1.27.1.nupkg")
INC = os.path.join(ROOT, "include")
LIB = os.path.join(ROOT, "lib")

INC_PREFIX = "build/native/include/"
NATIVE_PREFIX = "runtimes/win-x64/native/"


def main():
    os.makedirs(ROOT, exist_ok=True)
    print("Downloading", URL)
    urllib.request.urlretrieve(URL, NUPKG)
    print("Downloaded", os.path.getsize(NUPKG), "bytes ->", NUPKG)

    for d in (INC, LIB):
        if os.path.isdir(d):
            shutil.rmtree(d)
        os.makedirs(d, exist_ok=True)

    inc_count = 0
    lib_files = []
    with zipfile.ZipFile(NUPKG) as z:
        for n in z.namelist():
            if n.startswith(INC_PREFIX) and not n.endswith("/"):
                rel = n[len(INC_PREFIX):].replace("/", os.sep)
                dst = os.path.join(INC, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with z.open(n) as src, open(dst, "wb") as out:
                    shutil.copyfileobj(src, out)
                inc_count += 1
            elif n.startswith(NATIVE_PREFIX) and (
                n.endswith("onnxruntime.dll") or n.endswith("onnxruntime.lib")):
                base = os.path.basename(n)
                with z.open(n) as src, open(os.path.join(LIB, base), "wb") as out:
                    shutil.copyfileobj(src, out)
                lib_files.append(base)

    print("include files extracted:", inc_count)
    print("lib files extracted:", sorted(lib_files))

    ok = (inc_count > 0
          and "onnxruntime.dll" in lib_files
          and "onnxruntime.lib" in lib_files)
    print("ONNX_OK" if ok else "ONNX_INCOMPLETE")
    sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()
