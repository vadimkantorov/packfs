import os
import sys
import ctypes

#lib = ctypes.CDLL(os.path.abspath('libpackfs.so'))
#lib.packfs_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
#lib.packfs_init.restype = ctypes.c_int

#print(lib.packfs_init(b'', b'packfs0.zip@/packfs/'))

print(open(sys.argv[1]).read())
