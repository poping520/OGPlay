# Game archive fixtures

Generated with 7zz built from the pinned upstream 7-Zip 26.06 source in a temporary checkout. The inputs are synthetic.

- `solid.7z` has one solid block with BCJ2 and three LZMA2 streams. `game.apk` contains `fixture APK bytes`. The OBB holds 257 copies of bytes `e8 01 00 00 00 e9 02 00 00 00`. `存档.txt` contains `non-ASCII name` and a newline.
- `encrypted.7z` encrypts the payload of the same `game.apk`. `encrypted-header.7z` also encrypts the header. The fixture password is `fixture`.

Creation flags for the solid archive were `-m0=BCJ2 -m1=LZMA2:d=64k -m2=LZMA2:d=64k -m3=LZMA2:d=64k -mb0:1 -mb0s1:2 -mb0s2:3 -ms=on -mmt=1`. All fixtures use `-mtc=off -mta=off -mtm=off`. The encrypted fixtures use `-pfixture` and `-mhe=off` or `-mhe=on`.
