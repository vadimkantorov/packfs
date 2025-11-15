set -ex

#zip -r packfs.zip packfs.c .git
#zip -0 -r packfs0.zip packfs.c .git
LD_PRELOAD=$PWD/libpackfs.so /usr/bin/stat packfs.zip
LD_PRELOAD=$PWD/libpackfs.so /usr/bin/stat packfs.zip/packfs.c
LD_PRELOAD=$PWD/libpackfs.so /usr/bin/stat packfs.zip/
LD_PRELOAD=$PWD/libpackfs.so /usr/bin/ls -lah packfs.zip
LD_PRELOAD=$PWD/libpackfs.so /usr/bin/cat packfs.zip/packfs.c
LD_PRELOAD=$PWD/libpackfs.so PACKFS_CONFIG="packfs.zip" /usr/bin/cat /packfs/packfs.c
LD_PRELOAD=$PWD/libpackfs.so /usr/bin/ls -lah packfs.zip/packfs.c
LD_PRELOAD=$PWD/libpackfs.so /usr/bin/find packfs.zip

LD_PRELOAD=$PWD/libpackfs.so PACKFS_CONFIG="packfs.zip" python test.py /packfs/packfs.c

rm -rf test || true
mkdir test && echo aa > test/a.txt && zip -r testa.zip test && rm -rf test
mkdir test && echo bb > test/b.txt && zip -r testb.zip test && rm -rf test
LD_PRELOAD=$PWD/libpackfs.so PACKFS_CONFIG="testa.zip@/packfstest:testb.zip@/packfstest" /usr/bin/find /packfstest
mkdir test && mv testa.zip testb.zip test
LD_PRELOAD=$PWD/libpackfs.so PACKFS_CONFIG="test/" /usr/bin/find /packfs
#echo '[{"path" : "test/", "size": 0, "offset": 0}, {"path" : "test/a.txt", "size": 3, "offset": 0}]' > test/testa.zip.json
echo '[{"path" : "test/a.txt", "size": 3, "offset": 0}]' > test/testa.zip.json
LD_PRELOAD=$PWD/libpackfs.so PACKFS_CONFIG="test/testa.zip.json" /usr/bin/find /packfs

#./packfs --input-path bar          --object
#./packfs --input-path foo/         --object
#./packfs --input-path foo          --object
#./packfs --input-path packfs.c     --object
#./packfs --input-path .git/        --object
#./packfs --input-path packfs.zip   --object
#./packfs --input-path packfs0.zip  --output-path packfs0.zip.json  --index
#./packfs --input-path packfs0.zip  --output-path packfs0.zip.json  --index --object

