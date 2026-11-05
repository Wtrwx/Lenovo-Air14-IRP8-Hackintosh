# MetalLifetimeCompat

Darwin 25.6.0と実際のIntel 8086:A7A0上で、厳密に一致するTiger LakeのMetal
ドライバーを対象とする実験的な寿命管理アダプターです。単独のコマンドバッファーごとに
残ってしまう、キューに保持されていないターゲット表を、本来のデストラクターで解放します。
エンコード中の単独バッファーフラグとGPUへの投入動作は維持します。共有状態や所有権を
確認できない場合の処理は本来のドライバーに任せます。

ドライバーの識別情報、不変のコード、元のコンパニオン、実行時のObjective-C ivarの
型とオフセットを検証します。CPU再生試験では元のリークを再現し、ネイティブの解放、
スーパークラスのサイズ変更、並行した寿命管理をAddressSanitizerと
UndefinedBehaviorSanitizerで検証します。実機でのメモリー増加、リモート画面とGPUの
安定性は**未検証**です。

ビルド方法：

```sh
python3 build.py --donor /path/to/AppleIntelTGLGraphicsMTLDriver.bundle \
  --output /path/to/new-private-directory
```

ビルダーはローカル署名付きの非公開バンドル候補を生成し、元のコンパニオンへのリンクを
検証します。インストール、再起動、Apple Metalドライバーのロード、GPUへの投入は
行いません。このリポジトリの対応するWEXソースは、サンドボックス内のWebKit GPU
プロセスに必要なコンパニオンの厳密なパスを許可します。復元用に導入済みMetalバンドル
全体と変更前のWEXを保存してください。

Appleのバイナリーと生成したバンドルは非公開のビルド入出力として扱います。
ここではMITライセンスの独自ソースのみ配布します。ドライバーのパッケージ化と、
維持している互換性コンパニオンは
[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics)に基づきます。
