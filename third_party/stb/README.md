# stb_image

An image decoder by Sean Barrett, <https://github.com/nothings/stb>,
dual-licensed MIT or public domain (see `LICENSE`). Only `stb_image.h`
(v2.30) is included, unchanged, from commit
`f58f558c120e9b32c217290b80bad1a0729fbb2c`. Reluva uses it through
`src/common/image.cpp` (TGA, JPEG, PNG and BMP decoding) and for its
deflate decoder in `src/common/zip.cpp`.
