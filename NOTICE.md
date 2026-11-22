# Third-party notices

7zipLX reuses 7-Zip source code.

Upstream: 7-Zip 26.06, Copyright (C) 1999-2026 Igor Pavlov.
The source is fetched by `scripts/fetch-7zip.sh` and is not vendored in this
repository.

License summary (see `LICENSES/7zip-License.txt` for the authoritative text):

- Most files: GNU LGPL 2.1 or later.
- `CPP/7zip/Compress/Rar*`: GNU LGPL with the unRAR license restriction.
- `CPP/7zip/Compress/LzfseDecoder.cpp`: BSD 3-clause.
- `C/ZstdDec.c`: BSD 3-clause.
- `C/Xxh64.c`: BSD 2-clause.
- Some files are public domain where stated in the source.

The unRAR restriction is reproduced in `LICENSES/7zip-unRarLicense.txt`.
If RAR decompression is not desired, build with `DISABLE_RAR_COMPRESS=1` (and
optionally `DISABLE_RAR=1`). This must be reflected in the distribution notices
and the CMake options.

Qt 6 is available under LGPLv3, GPL, and commercial licenses. The final
distribution must satisfy the chosen Qt license, including relinking rights for
dynamic linking and notices.

Before publishing a binary release:

- Add the complete project and Qt license texts under `LICENSES/`.
- Review the final application license for compatibility with 7-Zip LGPL and Qt.
