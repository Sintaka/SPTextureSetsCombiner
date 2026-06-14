Combine multi texture in one folder with regular expressions names.\
11 times faster than Texture Set Combiner with multi-threads. (40s vs 7min31s)\
Works with Textures Export with Alpha (Mainly for Substance Painter).\
Test in 39 Texture Sets , 234 8k imgs to 6 imgs.\
\
The only thing you need to do is edit config.ini with your renderer\
\
e.g. prman (Default Color for OpenGL Normal is Hex RGBA: 7F7F7FFF):\
SOURCE_DIR = r"D:\texture\test1"\
DESTINATION_DIR = r"D:\texture\test1_combined"\
BASE_NAME = "Friston-3"\
TEXTURE_PATTERNS = [\
    ("_Diffuse.png",       BASE_NAME + "_Diffuse_srgbtex_acescg.png",      "#000000FF"),\
    ("_Emissive.png",      BASE_NAME + "_Emissive_srgbtex_acescg.png",     "#000000FF"),\
    ("_Specular.png",      BASE_NAME + "_Specular_srgbtex_acescg.png",     "#000000FF"),\
    ("_Normal.png",        BASE_NAME + "_Normal_raw.png",                  "#7F7FFFFF"),\
    ("_Displacement.png",  BASE_NAME + "_Displacement_raw.png",            "#7F7F7FFF"),\
    ("_Roughness.png",     BASE_NAME + "_Roughness_raw.png",               "#7F7F7FFF"),\
]\
\
Currently release on windows. you can recompile that easily to linux.

<img width="663" height="643" alt="aa29073ea0ea77b9d3e52e5750852aa8" src="https://github.com/user-attachments/assets/5e8b91b0-ff66-4e84-bf88-420e5ac7263a" />\
<img width="970" height="549" alt="屏幕截图 2026-06-15 045025" src="https://github.com/user-attachments/assets/7c38e774-0099-428a-8f92-2af205bd624d" />\


edit config.ini and hit run\
\
todo:\
UDIM support (you can write it manully if your model with a tiny number of udim sets)\
Texture format other than png.
