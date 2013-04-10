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
2. VisualStudio2010 # Express可
上記のもの以外を利用した場合に動作するかどうかは不明です(想定していません)。

■ビルド手順
1. TvtPlayの"src.zip"を展開
2. "Util.h"の //#define EN_SWC → #define EN_SWC
3. "TvtPlay.sln"を開いてビルド

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
・"/pattern/s"のように、かならずパターンを/と/sで囲ってください
・正規表現についての説明は[資料-2]などをみてください
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

■資料
1. Kazutaka Kurihara,
   "CinemaGazer: a System for Watching Video at Very High Speed,"
   http://arxiv.org/abs/1110.0864 , 2011
2. ECMAScript syntax,
   http://www.cplusplus.com/reference/regex/ECMAScript/
