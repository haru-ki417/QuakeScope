# ほかの人が作ったもの

QuakeScope は、次のソフトウェアを使っています。

| 名前 | 使っている所 | ライセンス | リポジトリに含むか |
|---|---|---|---|
| [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) | ファームウェア（OLED） | BSD | 含まない（Arduino のライブラリマネージャーで入れる） |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | ファームウェア（文字・図形） | BSD | 文字の形（glcdfont.c の ASCII 部分）だけ `web/js/font5x7.js` に含む |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | ファームウェア | MIT | 含まない |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | ファームウェア | LGPL-2.1 | 含まない |
| [manifold-3d](https://github.com/elalish/manifold) | ケースの STL を作る道具 | Apache-2.0 | 含まない（`npm install` で入れる） |
| [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) | ブラウザー版の WebAssembly を作る道具 | Apache-2.0 WITH LLVM-exception | 道具は含まない。`web/qs.wasm` には、その C/C++ ランタイム（wasi-libc・libc++）の一部が入る |

## Adafruit GFX Library（glcdfont.c）

```
Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```
