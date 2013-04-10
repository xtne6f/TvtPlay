高速鑑賞機能ソースコードについて
2012-06-17 ver.4: 再生速度の指定方法を微変更(以降の履歴はTvtPlay_Readme.txtへ)
2012-05-11 ver.3: TvtPlay本体にマージ + TvtPlay_Blacklist.txt を少し修正
2012-04-21 ver.2: bregonig.dllを不要に + ルビを区別できるようにした
2012-04-17 ver.1: 初版

■概要(必ず読む)
TSの字幕ストリームを利用し、CinemaGazer[後述資料-1] の手法を参考にしてTvtPlayに
高速鑑賞機能を実装したものです。TvtPlayソースコードの"Util.h"にある
  //#define EN_SWC
のコメントをはずすと機能が追加されます。各自研究目的にお使いください。
なお、当方は当該手法の著作者とは無関係であり、このプログラムをもって当該手法の評
価をおこなうことは"絶対に"しないでください。
4～8倍速視聴の常用は人体への影響が未知数です。自身の健康に注意し、かならず明るい
部屋で使用してください。

■用意するもの
1. Caption2Ass_PCR ver.0,2,0,2 もしくはTVCaptionMod2に添付の"Caption.dll"
2. VisualStudio(以下VS)2005 or 2010 # Express可、たぶん2008も可
上記のもの以外を利用した場合に動作するかどうかは不明です(想定していません)。

■ビルド手順
1. TvtPlayの"src.zip"を展開
2. "Util.h"の //#define EN_SWC → #define EN_SWC
3. "TvtPlay.sln"(VS2005なら"TvtPlay(vc8).sln")を開いてビルド

■使い方
TvtPlay.tvtp、TvtPlay_Blacklist.txt、および"Caption.dll"を"TvtPlay_Caption.dll"
にリネームして、TVTestの"Plugins"フォルダに配置します。なお、Caption2Ass_PCRに添
付のCaption.dllを使うときは、同じく添付の"Gaiji"フォルダをTVTest.exeのあるフォル
ダに配置します。
TvtPlayの再生位置の表示部分を右クリックすると、「字幕でゆっくり」という項目が追
加されているはずです。

■設定ファイルについて
以下は追加される設定キーの説明です。
CaptionDll
    "Caption.dll"の場所を絶対パスかTVTestフォルダからの相対パスで指定
    # デフォルトは[=Plugins\TvtPlay_Caption.dll]
    # 同時に利用する他のプラグインが使うファイルと同じものを指定してはいけない
BregonigDll
    "bregonig.dll"([資料-2]のver.3.x系)の場所を絶対パスかTVTestフォルダからの相
    対パスで指定
    # デフォルトは[=](指定なし)
    # 指定なしか存在しない場合は組みこみのT-Rexライブラリを使います
    # 検索量は微々たるものなので、T-Rexの正規表現で足るならば指定なしのほうが性
    # 能面で有利です
SlowerWithCaption
    字幕のある区間での再生速度を指定
    # [=正数]のとき、{字幕のある区間での再生速度}＝{再生速度}×{設定値}÷100
    # [=負数]のとき、{字幕のある区間での再生速度}＝－{設定値}
    # [=0]で機能オフ
    # ただし、どのような設定値でも字幕のある区間での再生速度は100%を下回らないよ
    # うに調整されます。
    # この設定は再生位置の表示部分を右クリックで変更できます。
SlowerWithCaptionShowLate / SlowerWithCaptionClearEarly
    字幕のある区間の開始/終了を遅らせる/早める(ミリ秒)
    # [=-5000]から[=5000]まで
    # デフォルトは[=450]/[=-450](=450ミリ秒だけ字幕区間を遅らせる)
    # 設定値を大きくするとより短時間で視聴できますが、セリフの前後が聞きとりづら
    # くなるかもしれません。

■"TvtPlay_Blacklist.txt"について
・このファイルで、"字幕のある区間"から除外したい字幕パターンを一行ずつ、正規表現
  で指定してください。パターンのマッチ状況はWinDbgなどのデバッガで確認(マッチす
  ると"[X]"と出力)できます
・字幕文字列の一部はタグで装飾されます。たとえば、ルビに使われる小型サイズの文字
  列は"\<SSZ>るび\</SSZ>"といった具合になります
・組みこみのT-Rexライブラリを使う場合は、"/pattern/s"のように、かならずパターン
  を/と/sで囲ってください
・正規表現についての説明は[資料-2]や、後述「T-Rexで利用可能な正規表現」などをみ
  てください
・マッチさせたいARIB外字(ARIB STD-B24 第一分冊参照)があるとき:
  <Caption2Ass_PCR の Caption.dll>
    "Gaiji"フォルダの"UNICODE_cc_*.ini"を弄って何らかの文字に置きかえてください
  <TVCaptionMod2 の Caption.dll>
    上記STD-B24の表7-19および表7-20にしたがってUnicodeに置換されるので、対応する
    文字をIMEパッドで植えるか、Caption2Ass_PCR.exeをつかって(添付のCaption.dllを
    TVCaptionMod2のCaption.dllに置きかえる)字幕を出力し、対応する文字をコピペし
    てください

■その他
・TVCaptionなど字幕プラグインとの併用をおすすめします
・[資料-1]の手法のエッセンスをすべて実装したわけではありません
・x64ビルドはTVCaptionMod2に添付のx64版のCaption.dllを利用してください
・2chの関連スレッドにもたまに書き込んでるので参考にしてください
・This software uses T-Rex 1.3 regex library written by Alberto Demichelis

■資料
1. Kazutaka Kurihara,
   "CinemaGazer: a System for Watching Video at Very High Speed,"
   http://arxiv.org/abs/1110.0864 , 2011
2. 正規表現ライブラリ bregonig.dll,
   http://homepage3.nifty.com/k-takata/mysoft/bregonig.html
3. T-Rex a tiny regular expression library
   http://tiny-rex.sourceforge.net/

■T-Rexで利用可能な正規表現
※正確な情報は[資料-3]のreadme.txtを参照(ただし\W/\wの説明は間違っている)
○基本
\ | () [] →大体bregonig.dllと同じ
^       文字列先頭
.       任意文字(改行を含む)
$       文字列末尾

○量指定子
* + ? {n} {n,} {n,m} →大体bregonig.dllと同じ

○エスケープ文字
\t      水平タブ(HT, TAB)
\n      改行(LF, NL)
\r      復帰(CR)
\f      改頁(FF)

○定義済文字クラス
※基本的に全角を含む
\l      小文字
\u      大文字
\A/\a   非/アルファベット
\W/\w   非/英数字と半角アンダーバー
\S/\s   非/空白文字
\D/\d   非/10進数字
\X/\x   非/16進数字
\C/\c   非/制御文字
\P/\p   非/区切り文字
\B/\b   非/単語境界
