using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Management;
using System.Net;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace StudySpace
{
    public class Atoms3InstallerForm : Form
    {
        private ComboBox cmbPort;
        private Button btnRefreshPort;
        private TextBox txtTermName;
        private TextBox txtWifiSsid;
        private TextBox txtWifiPass;
        private TextBox txtServerUrl;
        private CheckBox chkBuzzer;
        private CheckBox chkFlashFirmware;
        private ProgressBar pBar;
        private Label lblStatus;
        private TextBox txtLog;
        private Button btnInstall;
        private Button btnClose;

        private string baseDir;
        private string coreDir;
        private string appIcoPath;
        private string firmwareBinPath;
        private string esptoolExePath;

        [STAThread]
        public static void Main()
        {
            try
            {
                ServicePointManager.SecurityProtocol =
                    (SecurityProtocolType)3072 |
                    (SecurityProtocolType)768 |
                    (SecurityProtocolType)192;
            }
            catch { }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Atoms3InstallerForm());
        }

        public Atoms3InstallerForm()
        {
            baseDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\', '/');
            coreDir = Path.Combine(baseDir, ".core");
            if (!Directory.Exists(coreDir))
            {
                coreDir = baseDir;
            }

            appIcoPath = Path.Combine(coreDir, "app.ico");
            firmwareBinPath = Path.Combine(coreDir, "firmware_merged.bin");
            esptoolExePath = Path.Combine(coreDir, "esptool.exe");

            InitializeUI();
            RefreshComPorts();
            DetectCurrentWifiSsid();
        }

        private void InitializeUI()
        {
            this.Text = "StudySpace ATOMS3 カードリーダー インストーラー";
            this.Size = new Size(560, 650);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.BackColor = Color.FromArgb(248, 250, 252);

            if (File.Exists(appIcoPath))
            {
                try { this.Icon = new Icon(appIcoPath); } catch { }
            }

            Label lblTitle = new Label
            {
                Text = "StudySpace ATOMS3 カードリーダー セットアップ",
                Font = new Font("Meiryo UI", 13, FontStyle.Bold),
                ForeColor = Color.FromArgb(15, 23, 42),
                Location = new Point(24, 18),
                Size = new Size(500, 28)
            };
            this.Controls.Add(lblTitle);

            Label lblSub = new Label
            {
                Text = "M5Stack ATOMS3 Lite をUSBケーブルでPCに接続し、拠点名とWi-Fiを入力して【ATOMS3にインストール】を押してください。",
                Font = new Font("Meiryo UI", 9),
                ForeColor = Color.FromArgb(100, 116, 139),
                Location = new Point(24, 48),
                Size = new Size(500, 36)
            };
            this.Controls.Add(lblSub);

            int y = 94;

            // 1. COMポート選択
            Label lblPort = new Label
            {
                Text = "① USB接続ポート (ATOMS3 Lite):",
                Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold),
                ForeColor = Color.FromArgb(30, 41, 59),
                Location = new Point(24, y),
                Size = new Size(300, 20)
            };
            this.Controls.Add(lblPort);
            y += 24;

            cmbPort = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                Font = new Font("Consolas", 10f),
                Location = new Point(24, y),
                Size = new Size(390, 28)
            };
            this.Controls.Add(cmbPort);

            btnRefreshPort = new Button
            {
                Text = "再検出",
                Font = new Font("Meiryo UI", 9f),
                Location = new Point(424, y - 1),
                Size = new Size(96, 28),
                BackColor = Color.White,
                Cursor = Cursors.Hand
            };
            btnRefreshPort.Click += (s, e) => RefreshComPorts();
            this.Controls.Add(btnRefreshPort);
            y += 38;

            // 2. 拠点・設置場所名
            Label lblTerm = new Label
            {
                Text = "② 拠点・設置場所名（管理画面・打刻ログに表示される所属名）:",
                Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold),
                ForeColor = Color.FromArgb(30, 41, 59),
                Location = new Point(24, y),
                Size = new Size(480, 20)
            };
            this.Controls.Add(lblTerm);
            y += 24;

            txtTermName = new TextBox
            {
                Text = "那覇校 1F自習室",
                Font = new Font("Meiryo UI", 10f),
                Location = new Point(24, y),
                Size = new Size(496, 28)
            };
            this.Controls.Add(txtTermName);
            y += 38;

            // 3. Wi-Fi SSID & パスワード
            Label lblSsid = new Label
            {
                Text = "③ 設置場所の Wi-Fi SSID (2.4GHz帯):",
                Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold),
                ForeColor = Color.FromArgb(30, 41, 59),
                Location = new Point(24, y),
                Size = new Size(240, 20)
            };
            this.Controls.Add(lblSsid);

            Label lblPass = new Label
            {
                Text = "Wi-Fi パスワード:",
                Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold),
                ForeColor = Color.FromArgb(30, 41, 59),
                Location = new Point(280, y),
                Size = new Size(240, 20)
            };
            this.Controls.Add(lblPass);
            y += 24;

            txtWifiSsid = new TextBox
            {
                Font = new Font("Consolas", 10f),
                Location = new Point(24, y),
                Size = new Size(240, 28)
            };
            this.Controls.Add(txtWifiSsid);

            txtWifiPass = new TextBox
            {
                Font = new Font("Consolas", 10f),
                Location = new Point(280, y),
                Size = new Size(240, 28),
                UseSystemPasswordChar = false
            };
            this.Controls.Add(txtWifiPass);
            y += 38;

            // 4. サーバーURL
            Label lblUrl = new Label
            {
                Text = "④ 接続先サーバーURL:",
                Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold),
                ForeColor = Color.FromArgb(30, 41, 59),
                Location = new Point(24, y),
                Size = new Size(480, 20)
            };
            this.Controls.Add(lblUrl);
            y += 24;

            txtServerUrl = new TextBox
            {
                Text = "https://entry.studyspace.okinawa",
                Font = new Font("Consolas", 10f),
                Location = new Point(24, y),
                Size = new Size(496, 28)
            };
            this.Controls.Add(txtServerUrl);
            y += 36;

            // 5. オプションチェックボックス
            chkBuzzer = new CheckBox
            {
                Text = "Unit Buzzer (PORT B) で打刻時の効果音を鳴らす",
                Font = new Font("Meiryo UI", 9f),
                ForeColor = Color.FromArgb(30, 41, 59),
                Location = new Point(24, y),
                Size = new Size(480, 22),
                Checked = true
            };
            this.Controls.Add(chkBuzzer);
            y += 26;

            chkFlashFirmware = new CheckBox
            {
                Text = "ファームウェア本体 (.bin) の書き込みも実行する（初回必須・設定変更のみの場合はOFFで高速反映）",
                Font = new Font("Meiryo UI", 8.5f),
                ForeColor = Color.FromArgb(71, 85, 105),
                Location = new Point(24, y),
                Size = new Size(500, 22),
                Checked = File.Exists(firmwareBinPath)
            };
            this.Controls.Add(chkFlashFirmware);
            y += 32;

            // 進捗バー＆ステータス
            lblStatus = new Label
            {
                Text = "待機中：ATOMS3 Lite をUSB接続してボタンを押してください。",
                Font = new Font("Meiryo UI", 9f),
                ForeColor = Color.FromArgb(71, 85, 105),
                Location = new Point(24, y),
                Size = new Size(496, 20)
            };
            this.Controls.Add(lblStatus);
            y += 22;

            pBar = new ProgressBar
            {
                Location = new Point(24, y),
                Size = new Size(496, 18),
                Minimum = 0,
                Maximum = 100,
                Value = 0
            };
            this.Controls.Add(pBar);
            y += 26;

            // ログ表示欄
            txtLog = new TextBox
            {
                Multiline = true,
                ReadOnly = true,
                ScrollBars = ScrollBars.Vertical,
                Font = new Font("Consolas", 8.5f),
                BackColor = Color.FromArgb(15, 23, 42),
                ForeColor = Color.FromArgb(226, 232, 240),
                Location = new Point(24, y),
                Size = new Size(496, 100)
            };
            this.Controls.Add(txtLog);
            y += 112;

            // 実行ボタン
            btnInstall = new Button
            {
                Text = "⚡ ATOMS3 にインストール ＆ 初期設定を書き込む",
                Font = new Font("Meiryo UI", 10.5f, FontStyle.Bold),
                BackColor = Color.FromArgb(16, 185, 129),
                ForeColor = Color.White,
                FlatStyle = FlatStyle.Flat,
                Location = new Point(24, y),
                Size = new Size(380, 44),
                Cursor = Cursors.Hand
            };
            btnInstall.FlatAppearance.BorderSize = 0;
            btnInstall.Click += OnInstallClick;
            this.Controls.Add(btnInstall);

            btnClose = new Button
            {
                Text = "閉じる",
                Font = new Font("Meiryo UI", 9.5f),
                BackColor = Color.FromArgb(226, 232, 240),
                ForeColor = Color.FromArgb(30, 41, 59),
                FlatStyle = FlatStyle.Flat,
                Location = new Point(414, y),
                Size = new Size(106, 44),
                Cursor = Cursors.Hand
            };
            btnClose.FlatAppearance.BorderSize = 0;
            btnClose.Click += (s, e) => this.Close();
            this.Controls.Add(btnClose);
        }

        private void AppendLog(string msg)
        {
            if (this.InvokeRequired)
            {
                this.BeginInvoke(new Action<string>(AppendLog), msg);
                return;
            }
            txtLog.AppendText(string.Format("[{0:HH:mm:ss}] {1}\r\n", DateTime.Now, msg));
        }

        private void SetProgress(int val, string status)
        {
            if (this.InvokeRequired)
            {
                this.BeginInvoke(new Action<int, string>(SetProgress), val, status);
                return;
            }
            pBar.Value = Math.Max(0, Math.Min(100, val));
            lblStatus.Text = status;
        }

        private void RefreshComPorts()
        {
            cmbPort.Items.Clear();
            string[] ports = SerialPort.GetPortNames();
            Array.Sort(ports);
            foreach (string p in ports)
            {
                cmbPort.Items.Add(p);
            }
            if (cmbPort.Items.Count > 0)
            {
                cmbPort.SelectedIndex = cmbPort.Items.Count - 1;
                AppendLog("検出されたCOMポート: " + string.Join(", ", ports));
            }
            else
            {
                AppendLog("COMポートが見つかりません。ATOMS3 Lite をUSBケーブルで接続してください。");
            }
        }

        private void DetectCurrentWifiSsid()
        {
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo("netsh", "wlan show interfaces")
                {
                    RedirectStandardOutput = true,
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                using (Process p = Process.Start(psi))
                {
                    string output = p.StandardOutput.ReadToEnd();
                    foreach (string line in output.Split('\n'))
                    {
                        string trimmed = line.Trim();
                        if (trimmed.StartsWith("SSID") && !trimmed.StartsWith("BSSID"))
                        {
                            int idx = trimmed.IndexOf(':');
                            if (idx > 0)
                            {
                                string ssid = trimmed.Substring(idx + 1).Trim();
                                if (!string.IsNullOrEmpty(ssid))
                                {
                                    txtWifiSsid.Text = ssid;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            catch { }
        }

        private void OnInstallClick(object sender, EventArgs e)
        {
            if (cmbPort.SelectedItem == null)
            {
                MessageBox.Show("ATOMS3 Lite が接続されているCOMポートを選択してください。", "ポート未選択", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (string.IsNullOrWhiteSpace(txtTermName.Text))
            {
                MessageBox.Show("拠点・設置場所名を入力してください。", "入力エラー", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            string selectedPort = cmbPort.SelectedItem.ToString();
            string termName = txtTermName.Text.Trim();
            string ssid = txtWifiSsid.Text.Trim();
            string pass = txtWifiPass.Text.Trim();
            string serverUrl = txtServerUrl.Text.Trim();
            bool buzzerOn = chkBuzzer.Checked;
            bool doFlash = chkFlashFirmware.Checked;

            btnInstall.Enabled = false;
            Thread worker = new Thread(() => RunInstallSequence(selectedPort, termName, ssid, pass, serverUrl, buzzerOn, doFlash));
            worker.IsBackground = true;
            worker.Start();
        }

        private void RunInstallSequence(string portName, string termName, string ssid, string pass, string serverUrl, bool buzzerOn, bool doFlash)
        {
            try
            {
                // STEP 1: ファームウェア (.bin) の書き込み
                if (doFlash)
                {
                    if (!File.Exists(firmwareBinPath))
                    {
                        AppendLog("[INFO] firmware_merged.bin が同梱されていないため、USBシリアル初期設定のみ実行します。");
                    }
                    else
                    {
                        SetProgress(20, "STEP 1/2: ATOMS3 Lite へファームウェアを書き込み中...");
                        AppendLog("ファームウェア書き込み開始 (" + portName + ")...");

                        string flasherCmd = null;
                        string flasherArgs = null;

                        if (File.Exists(esptoolExePath))
                        {
                            flasherCmd = esptoolExePath;
                            flasherArgs = string.Format("--chip esp32s3 --port {0} --baud 460800 write_flash 0x0 \"{1}\"", portName, firmwareBinPath);
                        }
                        else
                        {
                            flasherCmd = "python";
                            flasherArgs = string.Format("-m esptool --chip esp32s3 --port {0} --baud 460800 write_flash 0x0 \"{1}\"", portName, firmwareBinPath);
                        }

                        ProcessStartInfo psi = new ProcessStartInfo(flasherCmd, flasherArgs)
                        {
                            RedirectStandardOutput = true,
                            RedirectStandardError = true,
                            UseShellExecute = false,
                            CreateNoWindow = true
                        };

                        using (Process proc = Process.Start(psi))
                        {
                            string stdOut = proc.StandardOutput.ReadToEnd();
                            string stdErr = proc.StandardError.ReadToEnd();
                            proc.WaitForExit();

                            if (!string.IsNullOrEmpty(stdOut)) AppendLog(stdOut.Trim());
                            if (proc.ExitCode != 0)
                            {
                                AppendLog("[ERROR] 書き込みエラー: " + stdErr);
                                SetProgress(0, "ファームウェアの書き込みに失敗しました。");
                                EnableInstallButton();
                                return;
                            }
                        }

                        AppendLog("ファームウェアの書き込みが完了しました！起動を待機しています...");
                        Thread.Sleep(2500);
                    }
                }

                // STEP 2: USBシリアル経由で拠点名・Wi-Fi・サーバーURLをNVSへ送信
                SetProgress(70, "STEP 2/2: 拠点名・Wi-Fi設定を ATOMS3 本体メモリへ保存中...");
                AppendLog("USBシリアル経由で初期設定を送信中...");

                string safeTermName = termName.Replace("|", " ");
                string safeSsid = ssid.Replace("|", "");
                string safePass = pass.Replace("|", "");
                string safeUrl = serverUrl.Replace("|", "");
                string cmdLine = string.Format("STUDYSPACE_SET|{0}|{1}|{2}|{3}|{4}\n",
                    safeTermName, safeSsid, safePass, safeUrl, buzzerOn ? "1" : "0");

                using (SerialPort sp = new SerialPort(portName, 115200))
                {
                    sp.Encoding = Encoding.UTF8;
                    sp.ReadTimeout = 4000;
                    sp.WriteTimeout = 4000;
                    sp.DtrEnable = true;
                    sp.RtsEnable = true;
                    sp.Open();

                    Thread.Sleep(300);
                    sp.DiscardInBuffer();
                    sp.Write(cmdLine);

                    DateTime deadline = DateTime.Now.AddSeconds(5);
                    bool ackReceived = false;
                    while (DateTime.Now < deadline)
                    {
                        try
                        {
                            string line = sp.ReadLine().Trim();
                            if (!string.IsNullOrEmpty(line))
                            {
                                AppendLog("[ATOMS3] " + line);
                                if (line.StartsWith("STUDYSPACE_OK|"))
                                {
                                    ackReceived = true;
                                    break;
                                }
                            }
                        }
                        catch (TimeoutException)
                        {
                            break;
                        }
                    }
                    sp.Close();

                    if (ackReceived)
                    {
                        AppendLog("✅ ATOMS3 本体メモリへの設定保存に成功しました！");
                    }
                    else
                    {
                        AppendLog("ℹ️ 設定コマンドを送信しました（応答待機タイムアウト）。");
                    }
                }

                SetProgress(100, "✅ セットアップ完了！ATOMS3 が自動的にWi-Fiへ接続し稼働開始します。");
                MessageBox.Show(
                    "ATOMS3 カードリーダーのセットアップが完了しました！\n\n" +
                    "・拠点名: " + termName + "\n" +
                    "・Wi-Fi SSID: " + (string.IsNullOrEmpty(ssid) ? "(事前登録リストを使用)" : ssid) + "\n\n" +
                    "リーダーが自動的に再起動してサーバーへ拠点名を登録します。",
                    "セットアップ完了",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                AppendLog("[ERROR] " + ex.Message);
                SetProgress(0, "エラーが発生しました: " + ex.Message);
                MessageBox.Show("セットアップ中にエラーが発生しました:\n" + ex.Message, "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                EnableInstallButton();
            }
        }

        private void EnableInstallButton()
        {
            if (this.InvokeRequired)
            {
                this.BeginInvoke(new Action(EnableInstallButton));
                return;
            }
            btnInstall.Enabled = true;
        }
    }
}
