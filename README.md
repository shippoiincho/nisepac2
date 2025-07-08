# 偽PAC2 for PASOPIA7

## WORK IN PROGRESS

## これはなに？

PASOPIA7 で PAC2 のまねをするモジュールです。
以下の機能があります。

- 漢字 PAC
- JoyPAC
- 64k RAMPAC x 2

PASOPIA7 では PAC2 は自動切換されます。
初代 PASOPIA でも動くはずですが動作確認とっていません。

---
## 回路

PAC2 用のコネクタの信号を、単純に Raspberry Pi Pico2 に接続しただけです。
Pico2 が 5V 耐性なのを良いことに直結しています。

![Schematics](/schematics.png)

PAC2 のコネクタは PASOPIA 正面から見て右側が Pin1 になっています。

![PAC2 Connector](/pac2connector.jpg)

PASOPIA に接続する前に、偽PAC2 の実行イメージ `nisepac2.uf2` を Pico に書き込んでください。

---
## 漢字PAC

漢字PAC のエミュレートをします。
あらかじめ picotool などで漢字 ROM のデータを書き込んでおく必要があります。

```
picotool.exe load -v -x kanji.rom  -t bin -o 0x10060000
```

---
## RAMPAC

64KiB の RAMPAC が 2 台接続されているように見えます。
Pico2 のフラッシュ上に 0 ~ 47 までの 48 台の RAMPAC のデータがあって、
その中の 2 台が接続されているように見えます。

RAMPAC の切り替えは、slot 5 に切り替え後、&H18 ないし &H19 へ出力することで行います。
Slot 4 の RAMPAC を切り替えるには以下のようにします

```
OUT &H1B,5:OUT &H18,2
```

電源オン時には、0 番と 1 番の RAMPAC がそれぞれ、Slot 4 と 3 に接続されているように見えます。
また、同じ番号の RAMPAC を両方のスロットに同時に接続することはできません。

RAMPAC のデータは、自動的に Pico2 のフラッシュと同期されます。
書き込みタイミングは RAMPAC を入れ替えた時、
または一定時間アクセスがないとき(5秒くらい)となっています。

未書き込みデータがあるかどうかは、以下のようにチェックできます。

```
OUT &H1B,5:PRINT INP(&H1A)
```

0 が帰ってくれば未書き込みデータはありません。

---
## JoyPAC (未テスト)

USB の DirectInput 対応ゲームパットまたは Joystick をサポートします。
