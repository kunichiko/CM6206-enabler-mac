# CM6206 Enabler for macOS

[English version](README.md)

CM6206チップを使用したUSBオーディオインターフェースの全機能をmacOSで有効にするための初期化ツールです。

## 概要

CM6206は、C-Media製の多チャンネルサウンドチップで、5.1または7.1サラウンドサウンドに対応した安価なUSBオーディオデバイスに広く使用されています。このチップは以下の製品に搭載されています：

- 各種ノーブランドUSB 5.1/7.1サウンドアダプター
- Zalman ZM-RS6Fヘッドフォン
- Diamond XS71U
- その他多数のUSBオーディオ製品

### 問題

CM6206チップは完全なUSB Audio準拠デバイスで、macOSを含む最新のOSではドライバ不要で認識されますが、**工場出荷時に音声出力が無効化されている**という奇妙な仕様があります。そのため、オーディオデバイスとして認識されても音が出ません。

また、**光デジタル入出力（S/PDIF）もデフォルトでは無効**になっています。

### 解決方法

このプログラムは、USB経由で初期化コマンドを送信し、以下の機能を有効化します：

- アナログ音声出力（全チャンネル）
- S/PDIF光デジタル入出力
- ステレオマイク入力

## 主な機能

- **ワンショットモード**: CM6206デバイスを検出して一度だけ初期化
- **デーモンモード**: 常駐してデバイスの接続やスリープ復帰時に自動的に初期化
- **スリープ対応**: macOS復帰時に自動的に再初期化

## 技術的な詳細

### USB制御コマンド

このプログラムは、CM6206のインターフェース2に対して以下の3つのUSB制御要求を送信します：

1. **REG0設定** (レジスタ 0x00 = 0xa004)
   - DMA_Master=1 (S/PDIF入力をマスタークロックに設定)
   - サンプリングレート=48kHz
   - Copyright=not asserted (S/PDIF出力のコピープロテクトフラグを無効化)

2. **REG1設定** (レジスタ 0x01 = 0x2000)
   - PLLBINen=1 (PLL binary search有効化)
   - S/PDIFおよびクロック生成を有効化

3. **REG2設定** (レジスタ 0x02 = 0x8004)
   - DRIVERON=1 (ライン出力ドライバー有効化)
   - EN_BTL=1 (ステレオマイク有効化)
   - 全アナログ出力チャンネルを有効化

### デバイス識別

- **Vendor ID**: 0x0d8c (C-Media Electronics Inc.)
- **Product ID**: 0x0102

## インストール方法

Homebrewを使用してインストールできます：

```bash
brew install kunichiko/tap/cm6206-enabler
```

## アップデート方法

```bash
# Homebrewのパッケージ情報を更新
brew update

# cm6206-enablerを最新版に更新
brew upgrade cm6206-enabler
```

または、全てのHomebrewパッケージをまとめて更新：

```bash
brew upgrade
```

現在のバージョンを確認：

```bash
cm6206-enabler -V
```

## 使用方法

### コマンドラインオプション

```
cm6206-enabler [-s] [-d] [-v] [-V]

オプション:
  -v  詳細モード：初期化の詳細メッセージを表示
  -s  サイレントモード（デフォルトと同じ、明示的にverbose出力を無効化）
  -d  デーモンモード：プログラムを常駐させ、デバイスの接続や
      スリープ復帰時に自動的に初期化
  -V  バージョン番号を表示して終了
```

### 基本的な使用例

```bash
# 接続されているCM6206デバイスを一度だけ初期化
./cm6206-enabler

# デーモンモードで起動（推奨）
./cm6206-enabler -d
```

### 自動起動設定

システム起動時やスリープ復帰時に自動的にデバイスを初期化するには、LaunchAgentまたはLaunchDaemonとして登録します。

#### LaunchAgent（推奨・簡単）

ユーザーログイン時に自動起動します。**sudo不要**で簡単に設定できます。

```bash
# インストール
cm6206-enabler install-agent

# アンインストール
cm6206-enabler uninstall-agent
```

**特徴**:
- ✅ sudoが不要
- ✅ ユーザーごとに設定可能
- ✅ macOS Ventura以降では「ログイン項目」として認識される
- ✅ コマンド一発でインストール完了
- ログ: `~/Library/Logs/cm6206-enabler.log`

#### LaunchDaemon（高度な用途）

システム起動直後から動作します。**sudoが必要**です。

```bash
# インストール（sudoが必要）
sudo cm6206-enabler install-daemon

# アンインストール（sudoが必要）
sudo cm6206-enabler uninstall-daemon
```

**特徴**:
- システム起動直後から有効
- スリープ復帰時の動作がより確実
- 全ユーザーで共通
- ログ: `/var/log/cm6206-enabler.log`

## システム要件

- macOS Tahoe (26.0) 以降で動作確認済み
- **注意**: 他のバージョンのmacOSでは動作確認ができていませんが、動作する可能性があります

## 制限事項

### プログラムの制限

- CM6206はAC3やDTSストリームの独立したデコードができません
- ステレオソースからサラウンドへのアップミキシング機能はありません
- S/PDIF出力はフロントチャンネルのみをミラーします
- macOS 10.7 Lion以降では、起動時またはスリープ復帰時にデバイスが接続されている必要があります

#### S/PDIF入力に関する重要な注意

このプログラムは、S/PDIF入力を有効化しますが、**SCMSコピープロテクション（Copyrightビット）が有効になっている信号はキャプチャできません**。

- CM6206は、S/PDIF信号のCopyrightビットを検出し、コピープロテクトがかかっている信号（Copyright=asserted）の録音をブロックします
- このプログラムでは、CM6206自身のS/PDIF出力信号のCopyrightビットを「not asserted」に設定していますが、外部機器から入力される信号のコピープロテクトチェックを無効化する方法は見つかっていません
- 一部の市販オーディオ機器（CDプレーヤーやゲーム機など）は、デフォルトでCopyrightビットを有効にして出力するため、これらの機器からの録音はできない可能性があります
- **現時点では、CM6206でコピープロテクトが有効な信号をキャプチャする方法は判明していません**

自作機器やコピープロテクトのない機器からのS/PDIF入力であれば、問題なくキャプチャできます。

### 互換性の問題

- macOS 10.4以前では、インターフェース2が「使用中」エラー（0x2c5）により開けない問題があります

## ライセンス

GNU General Public License v3.0以降

このプログラムは無保証で提供されます。自己責任で使用してください。

## 作者とクレジット

**オリジナル作者**: Alexander Thomas
**開発期間**: 2009年6月 - 2011年2月

**改変・メンテナンス**: Kunihiko Ohnaka
**改変期間**: 2025年12月 -

### バージョン履歴

- **1.0** (2009/06): 初回リリース
- **2.0** (2011/01): デーモンモード実装
- **2.1** (2011/02): スリープ時の遅延問題を修正
- **3.0.0** (2025/12): macOS Tahoe対応の大規模アップデート
  - 最新macOS対応（Tahoe 26.0以降）
  - S/PDIF入力対応とレジスタ設定の最適化
  - LaunchAgent/LaunchDaemonの簡単インストール機能
  - Homebrew対応とMakefile追加
  - デフォルトをサイレントモードに変更、メッセージ改善
  - ドキュメント整備（日英バイリンガルREADME、開発者ガイド）

## 参考資料

### オリジナルプロジェクト

このプロジェクトは、Alexander Thomasによる以下のオリジナルプロジェクトに基づいています：

- [https://www.dr-lex.be/software/cm6206.html](https://www.dr-lex.be/software/cm6206.html)

### 有用なリンク

- [ALSA mailing list discussion 1](http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25003.html)
- [ALSA mailing list discussion 2](http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25017.html)
- [Linux USB audio driver source](http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/sound/usb/usbaudio.c#L3276)

## トラブルシューティング

### デバイスが認識されない

以下のコマンドでCM6206デバイスが接続されているか確認できます：

```bash
# デバイス名で検索（最も簡単）
system_profiler SPUSBHostDataType | grep -B 5 -A 10 "USB Sound Device"

# または、Vendor IDで検索（デバイス名も表示される）
system_profiler SPUSBHostDataType | grep -B 6 -A 5 "USB Vendor ID: 0x0d8c"

# ioregコマンドを使用する場合
ioreg -p IOUSB -l -r -n "USB Sound Device"
```

CM6206は通常「**USB Sound Device**」という名前で認識されます。USB Vendor ID `0x0d8c`（C-Media Electronics社）、Product ID `0x0102` を持ちます。

### 音が出ない

1. macOSのサウンド設定で「**USB Sound Device**」が選択されているか確認
   - CM6206はmacOS上では「USB Sound Device」という名前で表示されます
2. プログラムを詳細モードで実行してエラーメッセージを確認：
   ```bash
   cm6206-enabler -v
   ```

## コントリビューション

バグ報告や機能リクエストは、GitHubのIssueでお願いします。

## 注意事項

作者自身が「手間のかからないサラウンドサウンドにはこの種のサウンドカードを推奨しない」と述べているように、このデバイスには多くの制限があります。より良いサラウンドサウンド体験を求める場合は、専用のサラウンドデコーダーの使用を検討してください。

---

## 開発者向け情報

### ソースからビルドする方法

Xcodeプロジェクトが含まれています：

```bash
# Xcodeでビルド
xcodebuild -project CM6206-enabler-mac.xcodeproj -configuration Release

# または、Makefileを使用
make build
make install  # /usr/local/binにインストール
```

または、Xcodeで`CM6206-enabler-mac.xcodeproj`を開いてビルドすることもできます。

### ソースからビルドした場合のアップデート方法

```bash
# リポジトリを最新版に更新
cd /path/to/CM6206-enabler-mac
git pull origin main

# リビルドして再インストール
make clean
make build
sudo make install
```
