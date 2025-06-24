# M5StickC Plus2 GPSロガー
M5StickC Plus2とM5Stack用GPSユニットを用いてGPSデータを取得し、MQTTブローカーへ送信し、ブラウザで確認できるようにするプログラムです。

## 主な機能
- GPSデータ取得 (緯度・経度・高度・時刻)
- MQTTブローカーへのデータ送信
- Leaflet.jsを用いたWeb地図で現在地と移動ログを確認

## 参考リポジトリ
本プロジェクトは以下のリポジトリを参考に作成しました。  
一部コードを改変し、独自の機能追加を行っています。

- [visualizing-m5atom-gps](https://github.com/nk-tamago/visualizing-m5atom-gps) by **nk-tamago**  
