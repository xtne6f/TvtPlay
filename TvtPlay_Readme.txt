TVTest TvtPlay Plugin ver.0.2(人柱版)

■概要
TVTest付属のBonDriver_UDPを使ってローカルTSファイルを再生するプラグインです。
BonDriver_File+TVTestPluginのUDP版みたいなものです。作ってから日が浅いので人柱募
集中です。

■動作環境
・Windows XP以降(ただしVistaは未確認)
・TVTest ver.0.7.6(32bit)以降(たぶん)、or対応するTVH264

■使い方
TVTestのPluginsフォルダにTvtPlay.tvtpを入れてください。
TVTest起動オプションに/tvtplayを含むか、最後のオプションに拡張子.ts .m2t .m2ts
いずれかのファイルパスを付加するとプラグインは有効になり、起動時にそのファイルを
開きます。/tvtplayを含む場合はファイルパスの有無、拡張子は任意です。
例:
  TVTest.exe /d BonDriver_UDP.dll /tvtplay [foo.ts]
  TVTest.exe /d BonDriver_UDP.dll foo.ts
  (TVTestのデフォルトのBonDriverに"BonDriver_UDP.dll"を指定している場合、
  /d BonDriver_UDP.dllは不要)

起動するとTVTestウインドウ下部にコントロールが表示されます(デザイン的に細いウイ
ンドウ枠(右クリック→バー枠→細いウインドウ枠を使用)推奨)。コントロール左側の7個
のボタンは左から、ファイルを開く、シーク(-60秒、-15秒、-5秒、5秒、15秒、60秒)で
す。コントロール中央はシークバーになっていて、左クリックで任意の位置にシーク、右
クリックで再生を一時停止/再開します。コントロール右側には再生位置および総再生時
間が表示されます。

■仕様
・総再生時間の表示は数秒以内の誤差がでる場合があります。また、総再生時間はファイ
  ル先頭および末尾の情報から算出した値なので、カット編集されている場合は不正確に
  なります
・26.5時間以上のTSファイルはおそらく正しく再生できません(Program Clock Reference
  が巡回する関係上)
・再生位置から一度に13.2時間以上離れた位置にシークすると、誤った位置に飛びます
・追っかけ再生に対応しています
・追っかけ再生中の総再生時間は推計です
・勢いでつくったので不具合もけっこうあるかも

■ソースについて
当プラグインの作成に当たり、EpgDataCap_BonのEpgTimerPlugInを参考にしました。また
、TSファイルの同期・解析のために、tsselect-0.1.8(
http://www.marumo.ne.jp/junk/tsselect-0.1.8.lzh)よりソースコードを改変利用してい
ます。また、TVTest(ver.0.7.20)からソースコードを流用しています。特に以下のファイ
ルはほぼ改変なしに流用しています(差分は"diff_TVTestStatusView_orig.txt"を参照)
  "Aero.cpp"
  "Aero.h"
  "BasicWindow.cpp"
  "BasicWindow.h"
  "ColorScheme.cpp"
  "ColorScheme.h"
  "DrawUtil.h"
  "DrawUtil.cpp"
  "Settings.cpp"
  "Settings.h"
  "StatusView.cpp"
  "StatusView.h"
  "Theme.cpp"
  "Theme.h"
ソースコメントに流用元を記述しています。流用部分については流用元の規約に注意し、
その他の部分は勝手に改変・利用してもらって構いません。

■更新履歴
ver.0.2 (2011-08-16)
・下記の修正では修正できてなかったため、さらに差し替えました
  ・修正分は"diff_02r1_02r2.txt"を参照
ver.0.2 (2011-08-15)
・バグ発見のため15日21時頃に差し替えました
  ・ファイルパス付で起動すると起動時にフリーズする不具合を修正(TVTestの起動完了
    を待たずにRESET_VIEWERを発行したため、TVTest内部でデッドロックした模様)
・スレッドで要望のあったもののうち、とりあえず容易な2点を追加した
  ・プラグインが有効なときはファイルをドラッグ&ドロップできるようにした
  ・キー割り当てを追加した
・コントロールの描画をすこし修正
・マルチディスプレイに対応したかもしれない(環境が無いので…)
ver.0.1 (2011-08-13)
・初版
