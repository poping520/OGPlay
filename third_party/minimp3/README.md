# minimp3 来源记录

- 上游：`https://github.com/lieff/minimp3`
- 固定 commit：`ea99364f61c14656440e8d77e9c233ccf3124633`
- `minimp3.h` SHA-256：
  `57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad`
- `LICENSE` SHA-256：
  `6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01`
- 许可：CC0 1.0 Universal；两份文件均从上述 commit 原样取得。

`tests/fixtures/audio/short-mp3.mp3` 来自同一 commit 的
`vectors/l3-he_free.bit`，SHA-256 为
`b55afc2a21492c4b3035d423f36fe87560ef34f65eb997da60af9b01f300362c`；仅改名，
字节不变。OGPlay 的适配代码位于 `src/audio/mp3.cpp`，不修改 vendor 文件。
