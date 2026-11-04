# モバイル Raptor Lake 向け SMCProcessor

[VirtualSMC の SMCProcessor](https://github.com/acidanthera/VirtualSMC/tree/1.3.8/Sensors/SMCProcessor) 1.3.8 を基に、Lilu が CPU 世代を判別できない Intel family 6 model `BA`（Raptor Lake-P）の温度監視を追加した単独プラグインです。物理 CPU のベンダー、モデルおよび CPUID の温度機能を確認します。それ以外の未知の CPU は引き続き対象外です。

追加した経路は、読み取り専用の Intel 温度 MSR を使用して、コアおよび CPU パッケージの DTS 温度を公開します。このモデルで RAPL 電力、電圧、ファンセンサーは有効化しません。コアのラベルは Lilu のトポロジーに従います。他のプラグインがトポロジーを再構築する場合、各 P コアと E コアを完全に区別できるとは限りません。CPU 全体の監視にはパッケージ温度を使用してください。

既存の VirtualSMC と Lilu を保持してください。OpenCore では VirtualSMC の後に SMCProcessor を一つだけ有効化します。生成されるバンドルは `SMCProcessor-RaptorP.kext`、実行ファイルは `Contents/MacOS/SMCProcessor` です。このソースは VirtualSMC のビルドや置き換えを行いません。実機での検証範囲と制限は[プロジェクトの状態](../../README.md)を参照してください。

macOS、Xcode Command Line Tools、Python 3.9+、MacKernelSDK、Lilu の開発用リソースが必要です。

```sh
python3 build.py --release --sdk /path/to/MacKernelSDK --lilu /path/to/Lilu.kext
clang++ -std=c++17 tests/thermal_admission.cpp -o /tmp/smcprocessor-admission
/tmp/smcprocessor-admission
```

CPU テストでは対応条件の境界を確認します。実際の温度変化と再起動後の継続動作は別途検証が必要です。標準の `sp78` は、小数部が 8 ビットの符号付きビッグエンディアン固定小数点です（例：`55 00` は 85°C）。

上流ソースの情報は [upstream.json](upstream.json) に記録しています。元の BSD-3-Clause ライセンスと著作権表示を [LICENSE.txt](LICENSE.txt) および各ソースに保持しています。
