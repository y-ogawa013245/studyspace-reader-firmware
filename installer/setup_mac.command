#!/bin/bash
# StudySpace ATOMS3 カードリーダー セットアップスクリプト (macOS用)

cd "$(dirname "$0")"
CORE_DIR="./.core"
if [ ! -d "$CORE_DIR" ]; then
  CORE_DIR="."
fi

echo "========================================================"
echo "  StudySpace ATOMS3 カードリーダー セットアップ (macOS)"
echo "========================================================"
echo ""

# 1. USBシリアルポートの検出
PORTS=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null)
if [ -z "$PORTS" ]; then
  echo "⚠️  ATOMS3 Lite のUSBポートが見つかりません。"
  echo "   USB Type-C ケーブルでMacに接続してから再実行してください。"
  read -p "Enterキーで終了..."
  exit 1
fi

PORT=$(echo "$PORTS" | head -n 1)
echo "✅ 検出されたUSBポート: $PORT"
echo ""

read -p "① 拠点・設置場所名を入力 (例: 那覇校 1F自習室): " TERM_NAME
if [ -z "$TERM_NAME" ]; then
  TERM_NAME="StudySpace リーダー"
fi

read -p "② Wi-Fi SSID (2.4GHz帯 / 空欄時は事前登録リストを使用): " WIFI_SSID
read -p "③ Wi-Fi パスワード: " WIFI_PASS
read -p "④ サーバーURL [https://entry.studyspace.okinawa]: " SERVER_URL
if [ -z "$SERVER_URL" ]; then
  SERVER_URL="https://entry.studyspace.okinawa"
fi
read -p "⑤ Unit Buzzer で効果音を鳴らしますか？ [Y/n]: " BUZZER_ANS
BUZZER_ON="1"
if [[ "$BUZZER_ANS" =~ ^[Nn]$ ]]; then
  BUZZER_ON="0"
fi

# ファームウェア (.bin) が同梱されている場合は esptool で書き込み
if [ -f "$CORE_DIR/firmware_merged.bin" ]; then
  echo ""
  echo "⏳ ATOMS3 Lite にファームウェアを書き込んでいます..."
  python3 -m pip install --quiet esptool pyserial 2>/dev/null
  python3 -m esptool --chip esp32s3 --port "$PORT" --baud 460800 write_flash 0x0 "$CORE_DIR/firmware_merged.bin"
  sleep 2
fi

echo ""
echo "⏳ 拠点名・Wi-Fi設定を ATOMS3 本体メモリへ保存しています..."
python3 - <<EOF
import time, sys
try:
    import serial
    with serial.Serial("$PORT", 115200, timeout=4) as ser:
        time.sleep(0.4)
        cmd = f"STUDYSPACE_SET|${TERM_NAME}|${WIFI_SSID}|${WIFI_PASS}|${SERVER_URL}|${BUZZER_ON}\n"
        ser.write(cmd.encode("utf-8"))
        deadline = time.time() + 4
        while time.time() < deadline:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                print("[ATOMS3]", line)
                if line.startswith("STUDYSPACE_OK|"):
                    break
except Exception as e:
    print("Serial config notice:", e)
EOF

echo ""
echo "========================================================"
echo "  ✅ ATOMS3 カードリーダーのセットアップが完了しました！"
echo "  拠点名: $TERM_NAME"
echo "========================================================"
read -p "Enterキーでウィンドウを閉じます..."
