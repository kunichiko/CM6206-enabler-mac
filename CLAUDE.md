# CLAUDE.md - AI開発者向けプロジェクトガイド

このドキュメントは、Claude CodeやAI開発アシスタントがこのプロジェクトを理解し、効果的に作業できるようにするための技術ガイドです。

## プロジェクト概要

**プロジェクト名**: CM6206 Enabler for macOS
**言語**: C
**フレームワーク**: IOKit (macOS USB通信フレームワーク)
**ビルドシステム**: Xcode
**ライセンス**: GPL v3.0+

### プロジェクトの目的

C-Media CM6206 USBオーディオチップは、工場出荷時に音声出力が無効化されている。このプログラムはUSB制御要求を送信してオーディオ出力とS/PDIF（光デジタル入出力）を有効化する。

## ファイル構成

```
CM6206-enabler-mac/
├── main.c                          # メインプログラム（全ロジック）
├── cm6206-enabler.1                    # manページテンプレート（未完成）
├── COPYING                         # GPLライセンス全文
├── LICENSE                         # ライセンスファイル
├── README.md                       # ユーザー向けドキュメント
├── CLAUDE.md                       # このファイル
├── CM6206-enabler-mac.xcodeproj/  # Xcodeプロジェクトファイル
└── Installer/                      # インストーラー関連ファイル
    ├── be.dr-lex.cm6206init.plist # LaunchDaemon設定
    ├── fixperm.sh                  # 権限修正スクリプト
    ├── README.rtf                  # インストーラー説明
    └── InstallMe.pmdoc/            # パッケージメーカードキュメント
```

## コアアーキテクチャ

### 1. プログラムの実行フロー

```
main()
  ├─ コマンドライン引数解析
  ├─ シグナルハンドラ設定 (SIGINT, SIGHUP)
  └─ モード分岐
      ├─ [通常モード] ActivateDevices() → 1回だけ実行して終了
      └─ [デーモンモード]
          ├─ IONotification設定（デバイス接続監視）
          ├─ 電源管理コールバック設定（スリープ/復帰監視）
          └─ CFRunLoop実行（永続的に監視）
```

### 2. 重要な関数とその役割

#### `main(int argc, const char *argv[])`
- エントリーポイント
- コマンドライン引数: `-d` (daemon), `-v` (verbose), `-s` (silent), `-V` (version)
- main.c:685-779

#### `ActivateDevices()`
- 接続されている全CM6206デバイスを検索して初期化
- IOKit APIを使用してUSBデバイスを列挙
- main.c:622-660

#### `DeviceAdded(void *refCon, io_iterator_t iterator)`
- デーモンモード時のデバイス接続コールバック
- 新しいデバイスが接続されると自動的に呼ばれる
- main.c:509-567

#### `dealWithDevice(io_service_t usbDeviceRef)`
- デバイスを開き、インターフェースイテレータを作成
- 第2インターフェース（nCount == 1）に対してdealWithInterface()を呼び出す
- main.c:345-456

#### `dealWithInterface(io_service_t usbInterfaceRef)`
- USB インターフェースを開き、initCM6206()を呼び出す
- main.c:286-342

#### `initCM6206(IOUSBInterfaceInterface183 **intf)`
- **最も重要な関数**: CM6206に初期化コマンドを送信
- 3つのwriteCM6206Registers()呼び出し
- main.c:245-283

#### `writeCM6206Registers(IOUSBInterfaceInterface183 **intf, UInt8 byte1, UInt8 byte2, UInt8 regNo)`
- USB制御要求を構築して送信する低レベル関数
- bmRequestType: kUSBOut, kUSBClass, kUSBInterface
- bRequest: 0x09
- wValue: 0x0200
- wIndex: 0x03
- main.c:218-241

#### `powerCallback(void *rootPort, io_service_t y, natural_t msgType, void *msgArgument)`
- 電源管理イベント（スリープ/復帰）のコールバック
- kIOMessageSystemHasPoweredOn時にActivateDevices()を実行
- main.c:666-680

## USB制御コマンドの詳細

### 制御要求のパラメータ

```c
req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBInterface);
req.bRequest = 0x09;
req.wValue = 0x0200;
req.wIndex = 0x03;  // Interface 3を指定
req.wLength = 4;
req.pData = buf;    // 4バイトのデータ
```

### データバッファのフォーマット

```c
buf[0] = 0x20;      // 固定値（コマンドヘッダー）
buf[1] = byte1;     // パラメータ1
buf[2] = byte2;     // パラメータ2
buf[3] = regNo;     // レジスタ番号
```

### 送信される3つのコマンド

#### コマンド1: レジスタリセット
```c
writeCM6206Registers(intf, 0x00, 0x00, 0x00);
// → buf = {0x20, 0x00, 0x00, 0x00}
```

#### コマンド2: SPDIF有効化
```c
writeCM6206Registers(intf, 0x00, 0x30, 0x01);
// → buf = {0x20, 0x00, 0x30, 0x01}
// レジスタ0x01に0x0030を書き込む
```

#### コマンド3: アナログ出力とマイク有効化
```c
writeCM6206Registers(intf, 0x04, 0x80, 0x02);
// → buf = {0x20, 0x04, 0x80, 0x02}
// レジスタ0x02に0x8004を書き込む
// ALSA USB driver曰く: "Enable line-out driver mode,
// set headphone source to front channels, enable stereo mic."
```

### コメントアウトされた追加コマンド

main.c:276-279に以下のコメントアウトされたコマンドがある：

```c
// "Enable DACx2, PLL binary, Soft Mute, and SPDIF-out"
// writeCM6206Registers(intf, 0x00, 0xb0, 0x01);

// "Enable all channels and select 48-pin chipset"
// writeCM6206Registers(intf, 0x7f, 0x00, 0x03);
```

これらはALSAメーリングリストから取得したが、作者の環境では不要だったためコメントアウトされている。

## デバイス識別

```c
#define kVendorID   0x0d8c  // C-Media Electronics Inc.
#define kProductID  0x0102  // CM6206 based device
```

### 注意: インターフェース番号

CM6206は複数のUSBインターフェースを持つ。このプログラムは**第2インターフェース（インデックス1、nCount == 1）**に対してのみ制御要求を送信する（main.c:436-437）。

## IOKit APIの使用パターン

### デバイス検索のフロー

```c
1. IOMasterPort() → mach port取得
2. IOServiceMatching(kIOUSBDeviceClassName) → マッチング辞書作成
3. CFDictionaryAddValue() → VendorID/ProductIDを追加
4. IOServiceGetMatchingServices() → マッチするデバイスを検索
5. IOIteratorNext() → 各デバイスに対して処理
```

### デバイスとインターフェースのオープン

```c
1. IOCreatePlugInInterfaceForService()
2. QueryInterface() → IOUSBDeviceInterface取得
3. USBDeviceOpen()
4. CreateInterfaceIterator()
5. 各インターフェースに対して:
   - IOCreatePlugInInterfaceForService()
   - QueryInterface() → IOUSBInterfaceInterface取得
   - USBInterfaceOpen() (失敗時はUSBInterfaceOpenSeize()を試行)
   - ControlRequest() → USB制御要求送信
   - USBInterfaceClose()
```

## エラーハンドリング

### ErrorName() 関数
main.c:105-189に定義された包括的なIOReturnエラーコード変換関数。すべての可能なIOKitエラーを人間が読める文字列に変換。

### CheckError() と ShowError()
main.c:191-209で定義。エラーコードを出力する便利なラッパー関数。

### パイプストール処理
```c
if (err == kIOUSBPipeStalled)
    (*intf)->ClearPipeStall(intf, pipeNo);
```

## デーモンモードの実装

### 通知システム

#### 1. デバイス接続通知
```c
IOServiceAddMatchingNotification(
    gNotifyPort,
    kIOFirstMatchNotification,  // デバイスが最初に見つかった時
    matchingDictionary,
    DeviceAdded,                // コールバック
    NULL,
    &gAddedIter
);
```

#### 2. デバイス切断通知
```c
IOServiceAddInterestNotification(
    gNotifyPort,
    usbDevice,
    kIOGeneralInterest,
    DeviceNotification,  // コールバック
    privateDataRef,
    &(privateDataRef->notification)
);
```

#### 3. 電源管理通知
```c
IORegisterForSystemPower(
    &rootPort,
    &notificationPort,
    powerCallback,  // コールバック
    &notifier
);
```

### CFRunLoop統合

```c
gRunLoop = CFRunLoopGetCurrent();
runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);
CFRunLoopAddSource(gRunLoop, runLoopSource, kCFRunLoopDefaultMode);
CFRunLoopRun();  // ここでブロック、通知を待つ
```

## 既知の問題と制限

### 1. macOS 10.4以前の互換性問題
- インターフェース2が"in use" (error 0x2c5)で開けない
- main.c:31-32でTODOとして記載
- 回避策としてUSBInterfaceOpenSeize()を試みるが不完全（main.c:310）

### 2. カーネルパニックリスク
- サードパーティオーディオエンハンサーとの競合
- main.c:558-560で1秒のsleep()により緩和
- 作者のコメント: "This is not strictly necessary but it seems to avoid kernel panics"

### 3. 32ビット制限
- macOS Catalina (10.15)以降では動作不可
- Xcodeプロジェクト設定で64ビット対応が必要

### 4. CM6206の機能制限
- AC3/DTSデコード不可
- ステレオ→サラウンドアップミキシング不可
- S/PDIF出力はフロントチャンネルのみ

## ビルドとデバッグ

### Xcodeビルド設定

```bash
xcodebuild -project CM6206-enabler-mac.xcodeproj -configuration Release
```

### VERBOSEモード

main.c:73で`#define VERBOSE`をアンコメントすると、追加のデバッグ情報が出力される：
- パイプ数（numPipes）
- インターフェース参照
- その他の内部状態

### コマンドラインデバッグ

```bash
# 詳細出力で実行
./cm6206-enabler -v

# デバイスの確認
system_profiler SPUSBDataType | grep -A 10 "C-Media"
```

## 開発履歴と設計意図

### 作者のコメント（main.c:12-19より）
> "This is genuine Frankenstein software, composed from lesser parts of Apple
> sample code, some previous USB camera thing I wrote, SleepWatcher, USB
> sniff logs, and the Linux ALSA drivers.
> I'm not very experienced in writing software that deals with USB, so it is
> entirely possible that this program will cause kernel panics under
> special circumstances. Use at your own risk.
> There's probably also a lot of opportunity to simplify and/or do things
> more efficiently."

### リバースエンジニアリングの情報源
1. **USB Sniffing**: Windowsで動作するC-Mediaドライバのパケットをキャプチャ
2. **Linux ALSA driver**: sound/usb/usbaudio.cのCM6206初期化コード
3. **メーリングリスト**: ALSA-userメーリングリストの議論
4. **Apple Sample Code**: IOKit USBサンプルコード
5. **SleepWatcher**: スリープ/復帰検出の参考

## 潜在的な改善点

### TODO（main.c:25-32より）
1. **全コマンドの解析とGUI作成**
   - S/PDIF オン/オフ
   - チャンネル設定
   - マイクロフォン設定（ステレオ/モノ/バイアス電圧）

2. **仮想ヘッドフォンサラウンドモード**
   - CM6206に組み込まれている可能性があるが未確認

3. **macOS 10.4以前への対応**
   - インターフェースの排他制御問題の解決

### 追加の改善案
1. **64ビット対応**: macOS Catalina以降で動作させる
2. **コードの簡素化**: 作者自身が認めるように最適化の余地あり
3. **エラーリカバリ強化**: より堅牢なエラーハンドリング
4. **設定ファイル対応**: ユーザーがコマンドをカスタマイズできるように

## セキュリティとパーミッション

### 必要な権限
- **root権限は不要**: ユーザー権限で実行可能
- IOKitフレームワークはユーザー空間からUSBデバイスにアクセス可能

### LaunchDaemon設定
`Installer/be.dr-lex.cm6206init.plist`:
- System起動時に自動実行
- rootユーザーとして実行（LaunchDaemonの標準動作）

## 参考資料

### コード内の有用なリンク
- http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25003.html
- http://www.mail-archive.com/alsa-user@lists.sourceforge.net/msg25017.html
- http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/sound/usb/usbaudio.c#L3276

### Apple Developer Documentation
- IOKit USB Device Interface Guide
- IOKit Fundamentals
- Core Foundation Run Loops
- Power Management in macOS

## 開発時の注意点

### このプロジェクトを修正する際の推奨事項

1. **既存のコードを尊重**: 作者のコメント通り、一見冗長に見えるコードにも理由がある可能性
2. **実デバイスでのテスト必須**: カーネルパニックのリスクがあるため、仮想環境では不十分
3. **バックアップ**: カーネルパニックが発生する可能性があるため、作業前にシステムバックアップ
4. **段階的な変更**: 一度に大きな変更をせず、小さな変更を繰り返しテスト

### コードレビューのチェックポイント
- USBリソースの適切なリリース（メモリリーク防止）
- エラーハンドリングの完全性
- スレッドセーフティ（CFRunLoopとコールバック）
- 電源管理イベントの正しい処理

## まとめ

このプロジェクトは以下の特徴を持つ：

- **シンプルだが効果的**: 約780行のCコードで完結
- **特定の問題を解決**: CM6206の初期化問題に特化
- **リバースエンジニアリングの成果**: USBパケット解析とALSAドライバー調査の結果
- **実用的な設計**: ワンショットとデーモンモードの2つの使い方
- **レガシーコード**: 2011年開発、32ビット時代の設計

このプロジェクトを理解することで、macOSでのUSBデバイス制御、IOKitの使い方、デーモンプロセスの実装方法を学ぶことができる。
