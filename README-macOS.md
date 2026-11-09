# putty-ranvis 0.85 — macOS (Apple Silicon) ポート

Windows向けの多言語化・表示拡張が加えられた PuTTY 0.85 フォーク（putty-ranvis）を、
Apple Silicon を搭載した Mac でビルドして動かせるようにしたものです。
GTK3 バックエンド（quartz）で動作し、⌘C / ⌘V によるコピー＆ペーストと、
Windows 版と同じ言語ファイル（`.lng`）による日本語表示が使えるようになっています。

## 移植 (Porting)

PuTTY そのものとその多言語化・拡張を行うフォーク (putty-ranvis) の著作権は Simon Tatham らに
ありますが、**Apple Silicon を搭載した Mac への移植、および macOS 向けの一連の実装
（⌘C / ⌘V によるコピー＆ペースト、`.lng` 言語ファイルによる日本語表示、窓版 PuTTYgen、
`.app` バンドルへのライブラリ同梱、`.dmg` 化）は MIZUTANI, Kimihiko (Kyoto University)
が行いました。**

> This macOS (Apple Silicon) port of PuTTY 0.85 / putty-ranvis — including the ⌘C/⌘V
> clipboard bindings, the `.lng`-based Japanese localisation, the GTK front end for
> PuTTYgen, and the self-contained application bundles and disk image — was done by
> MIZUTANI, Kimihiko (Kyoto University). PuTTY itself remains copyright Simon Tatham.

この帰属は各 `.app` の `Info.plist`（`NSHumanReadableCopyright`）にも入れられているので、
Finder の「情報を見る」にも表示されます。

動作確認済みの環境:

| 項目 | 内容 |
| --- | --- |
| OS | macOS 15.7.2（arm64 / Apple M-series） |
| コンパイラ | Xcode command line tools の clang |
| ビルド | CMake 3.x（`brew install cmake`） |
| GUI | GTK+ 3.24.51（`brew install gtk+3`、quartz バックエンド） |
| ホストロケール | `ja_JP.UTF-8` |

## ビルドとインストール

```sh
brew install cmake gtk+3

cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j8
cmake --install build --prefix /usr/local     # 任意のプレフィックス可
```

インストールすると次のものが得られます。

- `bin/`: `putty`, `pterm`, `plink`, `pscp`, `psftp`, `psusan`, `puttygen`, `pageant`
- `share/putty/lang/0411/*.lng`: 日本語の言語ファイル（Windows 版と同一のファイル）

X11 専用の `puttytel` は macOS ではビルドされません。
設定の保存先は他の Unix 版と同じ `~/.putty/sessions` です。

## ソースの git における置き方

改造済みのソースは https://github.com/kymizutani/putty-ranvis-mac で管理しています。
以下の Zip は git を使わない人のための物で、本体はこちらです。

| 物 | 内容 |
|---|---|
| remote `origin` | この fork（kymizutani/putty-ranvis-mac） |
| remote `ranvis` | Windows 版フォーク本体（ranvis/putty）。tag は fork に継承されないので、70 個ここから取ります |
| branch `ranvis2` | Windows 版フォーク側の流れ。port の commit は載せず、そのまま mirror にしておきます |
| branch `macos-port` | macOS への port。`ranvis-0.85` を base に、意味ごとに 12 commit |
| tag `ranvis-0.85` | 受け取ったソース package と**1 バイト違わず一致する**地点（port の base） |
| tag `macos-<版番号>` | port の release。Windows 版側の tag は `ranvis-<ver>` 方式なので、`macos-` を付けて取り違えません |

```sh
git clone https://github.com/kymizutani/putty-ranvis-mac.git
cd putty-ranvis-mac
git remote add ranvis https://github.com/ranvis/putty.git
git fetch ranvis 'refs/tags/*:refs/tags/*'
git switch macos-port
cmake -S . -B build && cmake --build build -j8
```

git の中からビルドすると、About の「Source commit:」には実際のコミット ID が入ります
（git でない場所からのビルドのときは、次の項の説明どおり別の文字が入ります）。

Windows 版フォーク側が更新されたときは、base を進めてから port を載せ替えます。

```sh
git fetch ranvis
git switch ranvis2 && git merge --ff-only ranvis/ranvis2
git rebase --onto ranvis/ranvis2 ranvis-0.85 macos-port     # 古い base を明示
```

release を作るのは、tag を付けたところです。

```sh
git tag -a macos-0.85-003 -m "macOS port build 003"
sh unix/macos-make-dmg.sh --build build --volname "PuTTY 0.85"
sh unix/macos-make-src-zip.sh
git push origin macos-port --tags
```

`.dmg` と `.zip` は git に入れず（`build/` もろとも `.gitignore`）、tag に添えて配布します。

## 日本語表示

Windows 版が使っている `windows/lang/0411/*.lng` を、そのまま（変換なしに）読み込みます。
ファイル自体は UTF-16LE（BOM 付き）なので、読み込み時に UTF-8 へ変換しています。

- **自動**: ホストのロケールが日本語（`LANG=ja_JP.UTF-8` など）なら、自動的に日本語になります。
  `ja` → `0411` の対応表も持っているので、`ja` だけでも拾えます。
- **検索する場所**（この順）:
  1. 環境変数 `PUTTY_LANGDIR`
  2. 実行ファイルと同じディレクトリの `lang/`
  3. `.app` バンドル内の `Contents/Resources/lang/`
  4. 実行ファイルの `../share/putty/lang/`
  5. インストール時のデータディレクトリ（`share/putty/lang/`、コンパイル時に組み込み）
  - また、実行ファイルと同じ場所にある `<プログラム名>.lng`（例: `putty.lng`）も調べます。
- **上書き**:
  - `PUTTY_LANG=none`（または `C`）→ 英語（原文）で表示
  - `PUTTY_LANG=0411` などで明示指定
  - `PUTTY_LANGFILE=/path/to/putty.lng` → 言語ファイルを直接指定（空文字列で無効化）
  - ロケールは `LC_ALL` → `LC_MESSAGES` → `LANG` の順で最初に設定されているものだけを使います。
    なので `LC_ALL=C` を指定すれば日本語化はオフになります。
- `putty` / `pterm` / `puttygen` / `pageant` / `plink` それ専用の言語ファイル名で検索し、
  Windows 版と同じ `CanonicalName` 間接参照（`pterm.lng` → `putty.lng`）もサポートしています。

翻訳が効く範囲は、Unix 版のソースに実際に存在する文字列です。
日本語ファイルの 787 項目のうち **479 項目**が Unix/GTK 版で実際に使われており、
設定ダイアログ（左側のカテゴリー一覧、各パネルの見出しと項目、ボタン）やエラーメッセージが日本語になります。
残りの 308 項目は Windows 版のメインウィンドウやシステムメニュー固有の文字列で、Mac 版には対応する UI がありません。
ウィンドウタイトルも「PuTTY 設定」のように翻訳されます。

カテゴリー名のうち `SSH`, `Telnet`, `X11`, `GSSAPI`, `Kex`, `Fonts`, `TTY`, `Rlogin`, `SUPDUP` などは
言語ファイルでも原文のままなので、そのまま英語で表示されます（固有名詞なので意図された動作です）。

また、macOS 版で初めて表示される「メタキーの選択」パネルの 3 項目
（`Choose the Meta key:` など）の翻訳を `windows/lang/0411/putty.lng` に追加しました。

## 翻訳が届いていなかった箇所

窓の部品を作る側と `.lng` のキーとが、必ずしも同じ文面で書かれていない箇所がありました。
そのままでは辞書を引いても見つからないので、次のようにしてあります。

- **`&` の位置は無視して引けるようにしました。** Windows のメニューは「Ne&w Session...」のように
  接頭字を示す `&` を文の中へ書き込みますが、こちら側の文面にはそれがありません。言語ファイルを読み込むとき、
  `&` を取り除いた形でも同じ項目として登録しておき、どちらの形でも引けるようにしました
  （`unix/uxl10n.c` の `lng_mnemonic_free()`）。表示する側でも、値に「(&C)」の形で付いている印字は
  取り除きます。ログファイル名の `&Y`（年）のように `&` が別のものである文字列はそのまま残します。
- **端末のメニュー**（Ctrl+右クリックで出るもの）は、このフロントエンドが自分で考えた文面を使っていたため、
  何も翻訳されていませんでした。`unix/window.c` の `MKMENUITEM` / `MKSUBMENU` が `l10n_menu_label()`
  を通るようにしました。「Copy All → すべてをクリップボードへコピー」のように Windows 側の言い回しが
  違うものだけが、`unix/uxl10n.c` に対応表として残っています。
- **設定画面の「変更...」「参照...」ボタンと「フォントの選択」**は、共通の窓部品ではなくこの
  フロントエンドのコードに直接書き込まれていたため、辞書を通っていませんでした。`unix/dialog.c` で
  引くようにしました。
- **各ページの説明文**（「PuTTY のウィンドウの設定」など）は、プログラム名を埋めた後の文面で辞書を
  引いていたため、キーが見つかりませんでした。`config.c` で `l10n_dupprintf()`（書式を先に翻訳してから
  値を埋める）を使うようにしました。
- **キーそのものが無かった文面**は、`windows/lang/0411/putty.lng` に 15 件追加しました。フォントの
  設定ページの項目（全角テキスト用のフォント、ボールド用のフォント、影付きボールドなど——このフォークが
  足した機能で、Windows 版の日本語版でも英語のままです）、ターミナルのリセット、クリップボードの二項目、
  「フォントの選択」の見出しなどです。

翻訳の抜けを数える道具を三つ、入れてあります。

| 道具 | 何をする |
|---|---|
| `tools/lang-coverage.py` | 言語ファイルの全項目を数えて、その文面が Unix 側のソースに実際に含まれるものか（含まれない＝Windows 専用の文面）に分けます。Font のページのキーが無かったのを見つけたのがこれです |
| `tools/l10ntest.c` | 与えた文字列が言語ファイルでどう訳されるかを表示します。決まった検査一式を走らせる使い方と、stdin に一行ずつ渡す使い方 |
| `cfgdump`（`-DWITH_CONFIG_DUMP=ON`） | 本物の設定ウィンドウを組み立てて、ツリー・タブ・項目・端末のメニューに載る文字列をすべて標準出力に出します |

言語ファイルはプログラム名で選ばれるので、これらの道具には「どのプログラムの言語ファイルを使うか」を
`PUTTY_PROGNAME` で教えます。

```sh
python3 tools/lang-coverage.py
cmake -S . -B build -DWITH_CONFIG_DUMP=ON -DWITH_L10NTEST=ON
cmake --build build --target cfgdump l10ntest
PUTTY_PROGNAME=putty PUTTY_LANGDIR="$PWD/windows/lang" build/l10ntest
PUTTY_PROGNAME=putty PUTTY_LANGDIR="$PWD/windows/lang" build/cfgdump > /tmp/dump.txt
```

残っている英語は、保存したセッション名や「ANSI Red」のような色名、鍵交換のアルゴリズム名、制御文字の
一覧など、もともと翻訳されていない（翻訳すべきでない）ものが中心です。

## コピー＆ペースト（⌘C / ⌘V）

macOS では ⌘ が Ctrl とは別扱いで、端末に送られるのも「Ctrl+C」ではなく修飾キー付きのキーイベントになります。
このためターミナル側で直接処理しています（`unix/window.c` の `handle_command_key()`）。

- **⌘C** … 選択中のテキストをシステムのクリップボード（他のアプリと同じもの）にコピー
- **⌘V** … システムのクリップボードから貼り付け
- ⌘ キーを Meta キーとして使う設定にしている場合は、このショートカットは無効になります（Meta として端末に渡すため）。
- ⌘ と Ctrl を同時に押す組み合わせや、それ以外の ⌘ を含むキー操作は従来どおり GTK（macOS のシステムキー処理）に渡されます。
- `Ctrl-Shift-C` / `Ctrl-Shift-V` によるクリップボード操作は、以前と同じく設定で変更できます。
- 実機で確認済み（`pterm` をコマンドラインから起動 / `PTerm.app` の両方）:
  画面の文字列をドラッグ選択 → ⌘C → `pbpaste` に同じ文字列が現れる → ⌘V → 端末に貼り付け。
  つまり他のアプリと同じ共有クリップボードでやり取りできます。

## 窓版 PuTTYgen（`puttygen-gtk`）

upstream の PuTTY では、鍵生成の窓口は Windows 版の PuTTYgen だけで、Unix 系にはコマンドラインの
`puttygen` しかありません（`.app` をダブルクリックしても何も起きなかったのは、これが理由です）。
このポートでは Gtk で作った窓版を追加して、`PuTTYgen.app` の中身にしています。

- ソースは `unix/gtkgen.c`。ビルドすると `puttygen-gtk`（インストールもされます）として作られます。
- 画面の文言は `windows/puttygen.c` と**同じ英語文字列**を使うように作ったので、
  `windows/lang/*/puttygen.lng` がそのまま効きます（日本語の翻訳はすでに揃っています）。
- できること
  - 鍵の生成: Ed25519 / Ed448 / ECDSA (nistp256・nistp384・nistp521) / RSA / DSA / SSH-1 (RSA)。
    素数で作る鍵では素数の探し方（確率的 / 証明済み / 証明済み・均等分布）を選べ、
    RSA では "強い" 素数を使うことも指定できます。生成は別のスレッドで走り、進行バーが出ます（Windows 版と同じ作り）。
  - 鍵の読み込み: `.ppk` / OpenSSH / ssh.com / SSH-1 を自動で見分け、パスフレーズはダイアログで聞きます。
  - 保存: PuTTY 形式 (`.ppk`)、SSH-1 の形式、OpenSSH 形式（新旧）、ssh.com 形式への変換、
    公開鍵（`authorized_keys` に貼る形の一行）の表示とファイル書き出し。
    「鍵 → 鍵の保存に関するパラメータ...」で PPK のバージョン (2 / 3) と Argon2 の設定ができます。
  - 鍵コメントの編集、指紋の表示（SHA256 / MD5 を切替可）。
  - ショートカット: ⌘G 生成、⌘O 読み込み、⌘S 秘密鍵を保存、⇧⌘S 公開鍵を保存、⌘Q 終了。
- この GTK (3.24 / GLib 2.90) では **GtkApplication は `quit` アクションを自動では作りません**（試験プログラムで
  `g_action_map_lookup_action(app, "quit")` が NULL を返すことを確認しました）。メニューの終了項目は
  `app.quit` に繋がっているので、アプリ側に `quit` アクションを登録してやらないとグレーのままになります。
  `unix/main-gtk-application.c`（PuTTY / PTerm 側）はもともと自分で登録しているので問題ありません。
- 同じ種類のはまり方で、この GTK には **`close-request` というシグナルがありません**（ライブラリの中に
  文字列としてすら入っていません）。窗口を閉じるときの処理は `delete-event` に繋ぎます
  （upstream の `unix/askpass.c` もそうしています）。存在しないシグナルに繋ぐと何も起きず、
  未保存の鍵があるときに確認が出ないまま終了していました。
- まだ対応していないもの: OpenSSH 証明書の読み込み・追加・削除。Windows 版の PuTTYgen にはこの機能が
  ありますが、使う場面が乏しいのでこの窓口では実装していません。必要なときはコマンドラインの
  `puttygen --certificate` を使ってください。

なお 2 番目の鍵を作ると `random_create` の assert で落ちていました。`random_setup_special()` は
プロセスに 1 回だけ呼ぶ関数（2 回呼ぶと PRNG が既にあるので assert）で、正しくは「起動時に 1 回設定し、
生成のたびに `random_reseed()` で追加のエントロピーを投入する」という順序です（Windows 版も同じ）。
`main()` で一度だけ呼ぶように直してあります。

## バージョンの表示について

About の窓（アプリメニュー → About）と `putty --version` には、`version.h` の `TEXTVER` がそのまま
出ます。`0.85-003` のように、ハイフンの後ろの三桁がこのポートのビルド番号で、配布する
`.dmg` / `.zip` の名前の末尾に付ける数字と同じものです（`putty-ranvis-0.85-macos-arm64-003.dmg` は
0.85-003 をそのまままとめた物、という意味です）。`.app` が Finder で名乗るバージョン（「情報を見る」に出る
やつ）も、同じ文字列を `version.h` から読み取って入れます。配布ファイルの名前も同じ文字列から
組み立てます（`.app` と同じく、スクリプトが `version.h` を読みに行きます）。SSH の接続時に名乗る文字列は `SSHVER` で、
`SSH-2.0-PuTTY-ranvis-0.85-003` のようになります。

「Source commit:」という欄は、upstream の自動化されたビルドが **git のチェックアウトから**作ったときに、
そのコミット ID を自動で書き込むためのものです（後でこの実行ファイルがどのソースから作られたものか
特定できるようにするため）。このポートのソースは git 付きで配っていないので、その代わりに「git の
チェックアウトから作ったものではない」旨を書くようにしました。git のあるツリーからビルドすれば、
今までどおり実際のコミット ID が出ます。別の文字を書かせたければ
`cmake -DPORT_SOURCE_PROVENANCE="..."` で差し替えられます。

## .app バンドル

`cmake --build build` を実行すると、ビルドディレクトリの `macos/` に次のアプリが作られます
（インストールはされません。ダブルクリックで起動できることが目的の成果物だからです）。

- `build/macos/PuTTY.app`
- `build/macos/PTerm.app`
- `build/macos/PuTTYgen.app`

バンドルの中身は `unix/make-macos-bundle.sh` が組み立てます。

- `Contents/MacOS/<プログラム名>` … 実行ファイル。ターミナル系は `puttyapp` / `ptermapp`
  （GtkApplication を使う方）を入れています。upstream の `putty.bundle` と同じ選択で、
  Finder から起動したときに最前面に来たりアプリケーションメニューが付いたりするためです。
- `Contents/Info.plist` … 生成します（`CFBundleIdentifier` は upstream の `putty.plist` /
  `pterm.plist` と同じ値を使い、`puttygen` / `pageant` は同じ命名で補っています）。
- `Contents/Resources/<プログラム名>.icns` … `windows/*.ico` を `sips` で変換したもの。
- `Contents/Resources/lang/0411/*.lng` … 言語ファイル。プログラムはここを探します。
- 最後に ad-hoc のコードサインをかけます。

`PuTTYgen.app` の中身は上で説明した窓版 `puttygen-gtk` です（バンドルの中では `puttygen` という名前に
しています。言語ファイルはプログラム名で探すためです）。

Pageant は `.app` にしていません。秘密鍵エージェントはウィンドウを持たない裏方のプロセスで、Mac には
それを出すトレーがないため、ダブルクリックしても何も起きなかったことにしてしまうからです
（macOS には標準の ssh-agent とキーチェーンがあるので、そもそも必要な場面も薄い）。
どうしても使いたいときはコマンドラインの `pageant` を使ってください。

`.app` に入っているのは GtkApplication 版の方なので、**コマンドラインオプションは使いません**
（`open PTerm.app --args -e …` のような指定は効きません。upstream のバンドルも同じ構成です）。
オプションを付けたい場合は、インストールした `bin/putty` / `bin/pterm` の方を使ってください。
アプリ内の「編集 → コピー / 貼り付け」メニューにも ⌘C / ⌘V が割り当てられていますが、
端末ウィンドウでは上記の ⌘C / ⌘V が先に処理され、必ず共有クリップボードが使われます。

**ライブラリはバンドル内に同梱されています。** `make-macos-bundle.sh` の後で `macos-embed-deps.sh` が
走り、`Contents/Frameworks/` に必要な dylib をコピーして参照を `@executable_path` 相対に書き換え、
gdk-pixbuf のローダモジュールとそのキャッシュ、GSettings のスキーマ、fontconfig の設定も
`Contents/Resources/` に収めて、`Info.plist` の `LSEnvironment` からそれらを参照するようにします
（Homebrew が無いマシンでも動くことが目的です）。各バンドル 25MB くらいになります。

- 中に入ったライブラリが Homebrew のものを指していないことは、バンドル内の全 Mach-O を走査して
  外を向いた参照が 0 本であること、起動時に読み込むライブラリの中に `/opt/homebrew` が 1 つも
  出てこないことで確認しています。
- 動かなくても動くようにも作ってあります。同じマシンで Homebrew を消したりバージョンを上げたりしても、
  バンドル内のコピーを使うので影響しません。
- データの場所は、プログラムが**自分で起動時に探します**（`unix/utils/macos_bundle_env.c`）。実行ファイルの
  実際の位置から `<Name>.app/Contents/Resources/...` を組み立てて、gdk-pixbuf・GSettings・fontconfig・GTK に
  教えます。だからバンドルはどこへコピーしても、どの場所から起動しても（Finder でもターミナルからでも）
  同じように動きます。`Info.plist` に絶対路径が書き込まれたままですが、
  自分で見つけた方を優先するので使われません。
- 別々の場所にコピーしたバンドルで、`LSEnvironment` を消して環境変数を何も設定しない状態で試して、
  同梱した gdk-pixbuf のキャッシュや GSettings のスキーマ（ファイル選択のダイアログに必要なもの）が
  見つかることを確認しました。ディスクイメージに収めた状態でも確認しています。
- 同梱していないもの: GTK の入力メソッドモジュールと印刷バックエンド（起動時に一度も読まれていない
  ことが確認できている）、fontconfig のフォントキャッシュ（マシン固有で、初回にユーザーのキャッシュ
  ディレクトリへ作り直される）、GTK のテーマや絵文字データ（もともと探索路径に入っていない）。
  Homebrew の GTK が X11 のライブラリもリンクしているため、それらは入ったままです（約 4MB）。
  SVG のローダはこの Homebrew には無いので、シンボリック画像の警告はそのまま出ます
  （`brew install librsvg` をしても、このバンドルには入りません）。

Finder から起動したプログラムには環境変数がまったく渡されないため、言語は macOS のユーザー設定から決めます
（システム設定 → 言語と地域 の言語設定、つまり `AppleLanguages` / `AppleLocale` を読みます）。
`CFLocaleCopyCurrent()` はバンドルの開発言語（`CFBundleDevelopmentRegion`）に引っ張られて
英語を返してしまうので使いません。システム設定でこのアプリだけ言語を変える機能にも対応しています。

他の場所に置きたければ普通にコピーしてください（例: `cp -R build/macos/PuTTY.app /Applications`）。

## ソースの Zip

改造済みのソース一式（この説明も含む）を Zip にまとめて配るようにしています。

```sh
sh unix/macos-make-src-zip.sh
```

名前は `version.h` の版番号から組み立てます（`0.85-003` なら
`build/macos/putty-ranvis-0.85-macos-port-src-003.zip`）。About に出る版番号、`.app` のバージョン、
tag、配布物名が全部同じ文字列から来るので、どれが新しく作ったものか名前で見分けられます。
git の中から作るときは**コミット済みの内容**から作ります（`git archive` 経由。未コミットの変更や
`build/` は入りません。`--commit <ref>` で任意の地点から作れます。git でない場所——Zip を展開した
ような——ではファイルをそのまま集めます）。

- 展開すると `putty-ranvis-0.85/` が現れます（732 個、12MB 程度、Zip で 2.9MB くらい）。
- 中の `README-macOS.md` がこのファイルです。ビルドと `.app` / `.dmg` の作り方が書いてあります。
- Mac に限らず、GTK3 が使える環境なら Unix 系的一般のビルド手順（`cmake -S . -B build`）で
  ビルドできます。macOS 向けの部品（`unix/gtkgen.c` の窓版 PuTTYgen、`unix/macos-*.sh`、
  `unix/utils/macos_bundle_env.c`）は Darwin でのみビルドに組み込まれます。
- 作った後は `unzip -t` でアーカイブ自体を検証します。

## ディスクイメージ（.dmg）

作った三つのアプリを、まとめて配れる形にします。

```sh
sh unix/macos-make-dmg.sh --build build --volname "PuTTY 0.85"
```

名前はこのソースの版番号とアーキテクチャから組み立てられ、`version.h` を読みに行きます
（`0.85-003` → `build/macos/putty-ranvis-0.85-macos-arm64-003.dmg`）。版番号を `--seq` で上書きも
できますし、`--dry-run` は名前を表示するだけで作りません。古い物を消すことはしません。

出来たイメージ（三つで 32MB くらい）の中に、`PuTTY.app` /
`PTerm.app` / `PuTTYgen.app`、`/Applications` へのリンク（アイコンをそこに落とすとインストールされる、
というよくある形）、それにこの説明（README-macOS.md）が入ります。

- 中のアプリの署名が壊れていないかを、イメージに入れる前にそれぞれについて確かめ、作った後に
  `hdiutil verify` と、イメージをマウントしての実行まで確認します。
- 読む専用の圧縮（UDZO）です。マウントした場所が変わってもアプリは自分の場所を探すので、
  別のマシンでそのまま動きます（ad-hoc 署名のままなので、初めて開くときは Gatekeeper の確認が出ます）。

## 変更したファイル

| ファイル | 内容 |
| --- | --- |
| `cmake/platforms/unix.cmake` | macOS（Darwin）で `OSX_GTK` を定義。これにより `NOT_X_WINDOWS`（Xlib 不要）、`SET_NONBLOCK_VIA_OPENPT`、`OSX_META_KEY_CONFIG`（メタキー設定）、`JUST_USE_GTK_CLIPBOARD_UTF8`（システム クリップボード利用）が有効になります。`PUTTY_DATADIR` の定義、`libiconv` の自動検出（macOS では `-liconv` が必要）も追加。 |
| `unix/uxl10n.c` | 空だった Unix 版の言語サポートを実装。`.lng` の読み込み（UTF-16LE→UTF-8、`%XX` エスケープの復元、INI 形式のセクション、ハッシュテーブル）、ロケールからの言語選択、`CanonicalName` 参照、`get_l10n_setting()`（`_LINECODEPAGE_` など）まで、Windows 版 `winl10n.c` と同じ公開 API を提供。Finder 起動用に、環境変数が無い場合は macOS のユーザー設定（`AppleLanguages` / `AppleLocale`、アプリ単位の設定も）から言語を決めます。言語ファイルの名前はプログラム名から決めるので、`puttyapp` のような名前で呼ばれている場合や `puttygen-gtk` のように接尾辞が付いている場合は、元のプログラム名の言語ファイルも探しに行きます。追加として、Windows が接頭字の印しに使う `&` を取り除いたキーでも引けるようにし（表示時に「(&C)」も取り除く）、`l10n_menu_label()` でメニュー文言の対応（`Copy All` → `C&opy All to Clipboard` など）も引けるようにしました。テスト用のプログラム（`tools/l10ntest.c` や `cfgdump`）は自分の名前に合った言語ファイルを持てないので、`PUTTY_PROGNAME` で「どのプログラムのふりをするか」を教えるようにしました。 |
| `unix/cfgdump.c` | （新規・既定では作りません）本物の設定ウィンドウを組み立てて、ツリー・タブ・項目や端末メニューに載る文字列を標準出力に出す確認用プログラム。`-DWITH_CONFIG_DUMP=ON` で作れます。翻訳の抜けを画面を見ずに数えるためのものです。 |
| `tools/l10ntest.c` | （新規・既定では作りません）文字列を言語ファイルに通すとどう出るかを表示する小さな検査プログラム。`-DWITH_L10NTEST=ON` |
| `tools/lang-coverage.py` | （新規）言語ファイルの全項目を、Unix 側のソースに含まれるもの／Windows 専用のものに分類して数えます。翻訳漏れのキーを探すためのもの。 |
| `config.c` | 各ページの見出し文で、プログラム名を埋めた後に辞書を引いていた箇所を `l10n_dupprintf()`（書式を先に翻訳する）に修正。 |
| `stubs/nol10n.c` | 新しい `l10n_menu_label()` の空実装を追加（言語ファイルを使わないビルド用）。 |
| `unix/macos-embed-deps.sh` | （新規）作った `.app` を自己完結させるスクリプト。`otool -L` で依存を辿って dylib を `Contents/Frameworks/` にコピーし参照を書き換え、gdk-pixbuf のローダとそのキャッシュ・GSettings のスキーマ・fontconfig の設定を `Contents/Resources/` に収めて `Info.plist` の `LSEnvironment` に書きます。コピーしたファイルの ad-hoc 署名まで行い、動かしたバンドルに実行し直すと記録した場所の書き直しだけ行います。 |
| `unix/utils/macos_bundle_env.c` | （新規）`.app` の中で起動したときに、実行ファイルの実際の位置から同梱データ（gdk-pixbuf のローダキャッシュ、GSettings のスキーマ、fontconfig の設定）の場所を組み立てて環境変数に設定する。場所を移しても、ターミナルから起動しても動くようにするためのもので、`putty` / `pterm` / `puttyapp` / `ptermapp` / `puttygen-gtk` の起動処理から呼ばれます。 |
| `unix/macos-make-dmg.sh` | （新規）三つの `.app` をまとめて配れる `.dmg` にするスクリプト。`/Applications` へのリンクとこの説明も入れます。入れる前に各アプリの署名を確認し、出来たイメージの検証とマウントまで行います。名前の末尾に 001, 002, … と連番を付けます（`--seq` で指定、`--dry-run` で名前だけ表示）。 |
| `unix/macos-make-src-zip.sh` | （新規）改造済みソースを Zip にまとめるスクリプト。git の中ならコミット済みの中身だけを集めます。作った後に `unzip -t` で検証します。名前と番号は `version.h` から。 |
| `unix/release-name.sh` | （新規）`version.h` の版番号から、配布物の名前（base・連番・展開後のトップ_DIR 名）を組み立てる補助スクリプト。About の版番号とファイル名が離れないようにするためのもの。 |
| `unix/next-sequence-number.sh` | （新規）同じ名前のファイルが並んでいるときに次の番号を探す補助スクリプト。版番号が付いていないソース（upstream など）から作る場合の決め方として使います。 |
| `version.h` | 既定だった「Unidentified build」を、このポートの版番号（`0.85-003`）に差し替え。About の窓・`--version`・SSH の名乗り（`SSHVER`）・`.app` が名乗るバージョンは、すべてここから取ります。末尾の三桁がportのビルド番号で、配布ファイル名の末尾の数字と同じです。 |
| `CMakeLists.txt`, `cmake/setup.cmake`, `cmake/gitcommit.cmake` | git のチェックアウトではない場所からビルドしたときに、About の「Source commit:」へ何がソースなのか書けるようにしました（既定は、この port のソース package から作ったという旨。git がある場所なら今までどおりコミット ID が出ます。`-DPORT_SOURCE_PROVENANCE=` で差し替え）。 |
| `unix/make-macos-bundle.sh` | （新規）`.app` バンドルを組み立てるスクリプト。実行ファイル、Info.plist、`windows/*.ico` からの `.icns`、`Contents/Resources/lang` の言語ファイル、ad-hoc のコードサインまで行います。 |
| `unix/gtkgen.c` | （新規）窓版の PuTTYgen。鍵の生成・読み込み・保存・形式変換・指紋表示。`windows/puttygen.c` と同じ英語文言を使うので、既存の `puttygen.lng` で日本語になります。 |
| `unix/CMakeLists.txt` | `uxl10n.c` を全 Unix バイナリがリンクする `utils` ライブラリに追加。言語ファイルを `share/putty/lang/` にインストール。`iso2022` を必要とするターゲットのリンクを修正。`test_lineedit` から重複になるスタブを外す（Apple のリンカは厳密）。Darwin では CoreFoundation をリンクし、`PuTTY.app` / `PTerm.app` / `PuTTYgen.app` を作るターゲットを追加（ターミナル系は `puttyapp` / `ptermapp`、PuTTYgen は `puttygen-gtk` を使用）。Pageant はウィンドウを持たないのでバンドルしていません。`.app` が名乗るバージョンは `version.h` の `TEXTVER` を読み取るようにしました（二か所に版番号を書かないようにするため）。 |
| `unix/window.c` | ⌘C / ⌘V の実装。加えて、`OSX_GTK` を有効にするとコンパイルできなかった既存コード（`clipboard_text_received()` の未定義変数 `length`）を修正。Ctrl+右クリックのメニューは `MKMENUITEM` / `MKSUBMENU` / About 表示を `l10n_menu_label()`・`l10n_dupprintf()` 経由にしました（それまでは一切翻訳されていませんでした）。 |
| `unix/putty.c` | 設定ウィンドウのタイトルを言語ファイル経由（「PuTTY 設定」）で生成。 |
| `unix/dialog.c` | 設定ダイアログ左側のカテゴリー一覧（`Session`, `Connection`, …）も言語ファイルで翻訳するように表示ラベルのみを通しました。内部でパネル識別に使うパスは未翻訳のままです（Windows 版と同じ作り）。フォント設定の「変更...」・ファイル選択の「参照...」ボタンと「フォントの選択」ウィンドウ見出しも、このファイルに直接書かれた文面だったので辞書を通すようにしました。 |
| `unix/pageant.c` | 共有部分のエージェント呼び出し `accept_agent_request()` / `notify_agent_make_op()` の Unix 版（不要なため空実装）を追加。 |
| `conf.h` | Windows 専用の既定値マクロ（`CW_USEDEFAULT`、壁紙関連）の非 Windows 版スタブ値を定義。 |
| `settings.c`, `utils/conf_data.c` | `winwallp.h` のインクルードを Windows 限定に。 |
| `unix/utils/sleep_ticks.c` | `putty.h` を読み込むよう修正（`Conf` などの型が必要）。 |
| `putty.h` | `l10n_dupprintf()` と `l10n_get_codepage_orig_name()` の宣言を追加。 |
| `test/test_conf.c` | `no_dbackspace` を BOOL のままテストしていた箇所を INT に修正。プラットフォームに関係なく `test_conf` がアサートで落ちていた問題です。 |
| `windows/lang/0411/putty.lng` | macOS 固有の 3 項目に加え、フォント設定ページの項目（全角用・ボールド用のフォント、影付きボールド）や「ターミナルをリセット」「クリップボードにコピー」など 15 項目を追加。いずれもこのフォークが Windows 版で翻訳し忘れていた文面です。 |

## テスト

`ctest` は登録されていないので、ビルドディレクトリでテスト実行ファイルを直接起動します。

```sh
cd build && for t in test_*; do [ -x "$t" ] && [ ! -d "$t" ] && ./$t; done
```

`test_bpp`, `test_cert_expr`, `test_conf`, `test_decode_utf8`, `test_host_strfoo`,
`test_lineedit`, `test_terminal`, `test_tree234`, `test_wildcard` はすべて成功します。
`test_unicode_norm` だけは UnicodeData.txt を標準入力に与えない限り usage エラーで終了します（もともとの仕様です）。

## 分かっている制約

- Windows 版フォーク固有の機能で Mac 側にないもの: 壁紙・タイル表示、IME 入力のカーソル色、
  Windows のシステムメニュー／システムフォント、`xferlimit` など。
  （設定項目そのものは残っていますが、Mac 版では表示に影響しません。）
- 日本語言語ファイルのうち Windows の UI にしかない 308 項目は、そもそも対応する UI がないため、表示されません。
- `.app` バンドルは GTK を同梱して自己完結していますが、ad-hoc の署名で公証は通していないため、
  他のマシンに持って行くと Gatekeeper の確認が一度出ます（右クリック → 開く で通せます）。
  1 バンドル 25MB 前後、三つをまとめた `.dmg` で 32MB くらいになります。
- アプリのアイコンは元の `.ico` が 48×48 なので、Dock で大きく表示するとぼやけます。
  大きなアイコンを用意すれば `Contents/Resources/*.icns` を差し替えられます。
- GTK3 が Homebrew で提供されているため、Qt やネイティブ Cocoa の外観にはなりません（メニューやダイアログは GTK のもの）。
