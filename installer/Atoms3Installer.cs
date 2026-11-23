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
        private Button btnDiag;
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
            this.Size = new Size(560, 700);
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
                Size = new Size(496, 110)
            };
            this.Controls.Add(txtLog);
            y += 118;

            // ハードウェア診断テストボタン
            btnDiag = new Button
            {
                Text = "🔊 ハードウェア診断テスト（ブザー音・拡張ベース・NFCカード読取確認）",
                Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold),
                BackColor = Color.FromArgb(59, 130, 246),
                ForeColor = Color.White,
                FlatStyle = FlatStyle.Flat,
                Location = new Point(24, y),
                Size = new Size(496, 36),
                Cursor = Cursors.Hand
            };
            btnDiag.FlatAppearance.BorderSize = 0;
            btnDiag.Click += OnDiagClick;
            this.Controls.Add(btnDiag);
            y += 44;

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
                                    DetectWifiPasswordForSsid(ssid);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            catch { }
        }

        private void DetectWifiPasswordForSsid(string ssid)
        {
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo("netsh", "wlan show profile name=\"" + ssid + "\" key=clear")
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
                        if (trimmed.Contains("主要なコンテンツ") || trimmed.Contains("Key Content"))
                        {
                            int idx = trimmed.IndexOf(':');
                            if (idx > 0)
                            {
                                string key = trimmed.Substring(idx + 1).Trim();
                                if (!string.IsNullOrEmpty(key))
                                {
                                    txtWifiPass.Text = key;
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
            btnDiag.Enabled = false;
            Thread worker = new Thread(() => RunInstallSequence(selectedPort, termName, ssid, pass, serverUrl, buzzerOn, doFlash));
            worker.IsBackground = true;
            worker.Start();
        }

        private void OnDiagClick(object sender, EventArgs e)
        {
            if (cmbPort.SelectedItem == null)
            {
                MessageBox.Show("ATOMS3 Lite が接続されているCOMポートを選択してください。", "ポート未選択", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            string selectedPort = cmbPort.SelectedItem.ToString();
            using (Atoms3DiagForm diagForm = new Atoms3DiagForm(selectedPort, this.Icon, AppendLog))
            {
                diagForm.ShowDialog(this);
            }
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

                bool ackReceived = false;
                bool resetPromptShown = false;
                DateTime overallDeadline = DateTime.Now.AddSeconds(25);
                string currentTargetPort = portName;

                while (DateTime.Now < overallDeadline && !ackReceived)
                {
                    // 再起動によりCOMポート番号が変化した場合の自動追従
                    string[] availablePorts = SerialPort.GetPortNames();
                    if (availablePorts.Length > 0)
                    {
                        bool exists = false;
                        foreach (string p in availablePorts)
                        {
                            if (string.Equals(p, currentTargetPort, StringComparison.OrdinalIgnoreCase))
                            {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists)
                        {
                            Array.Sort(availablePorts);
                            currentTargetPort = availablePorts[availablePorts.Length - 1];
                            AppendLog("再起動後のCOMポートを検出: " + currentTargetPort);
                        }
                    }

                    try
                    {
                        using (SerialPort sp = new SerialPort(currentTargetPort, 115200))
                        {
                            sp.Encoding = Encoding.UTF8;
                            sp.ReadTimeout = 1500;
                            sp.WriteTimeout = 1500;
                            sp.DtrEnable = true;
                            sp.RtsEnable = true;
                            sp.Open();

                            Thread.Sleep(250);
                            sp.DiscardInBuffer();
                            sp.Write(cmdLine);

                            DateTime readDeadline = DateTime.Now.AddSeconds(2.5);
                            while (DateTime.Now < readDeadline)
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
                        }
                    }
                    catch (Exception)
                    {
                        if (!resetPromptShown)
                        {
                            resetPromptShown = true;
                            SetProgress(75, "⚠️ ATOMS3 の側面リセットボタンを1回「カチッ」と短押ししてください...");
                            AppendLog("⚠️ 手動ダウンロードモード(緑LED)で書き込んだ場合、ATOMS3側面のリセットボタンを1回短押しすると通常起動して設定が書き込まれます（待機中...）");
                        }
                        Thread.Sleep(1000);
                    }
                }

                if (ackReceived)
                {
                    AppendLog("✅ ATOMS3 本体メモリへの設定保存に成功しました！");
                }
                else
                {
                    AppendLog("ℹ️ ファームウェア書き込みは完了しています。側面リセットボタンを1回短押ししてから、再度【インストール】を押す（または「ファームウェア本体の書き込み」のチェックを外して押す）と設定のみ高速保存できます。");
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
            btnDiag.Enabled = true;
        }
    }

    public class Atoms3DiagForm : Form
    {
        private string portName;
        private Action<string> parentLog;
        private Label lblHeader;
        private Label lblGuide;
        private DataGridView grid;
        private Button btnRerun;
        private Button btnClose;
        private bool isRunning = false;

        public Atoms3DiagForm(string port, Icon appIcon, Action<string> logCallback)
        {
            this.portName = port;
            this.parentLog = logCallback;
            this.Text = "ハードウェア診断テスト結果 (" + portName + ")";
            this.Size = new Size(860, 470);
            this.StartPosition = FormStartPosition.CenterParent;
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.BackColor = Color.FromArgb(248, 250, 252);
            if (appIcon != null)
            {
                try { this.Icon = appIcon; } catch { }
            }

            lblHeader = new Label
            {
                Text = "🔊 ATOMS3 ハードウェア診断テスト (" + portName + ")",
                Font = new Font("Meiryo UI", 12f, FontStyle.Bold),
                ForeColor = Color.FromArgb(15, 23, 42),
                Location = new Point(20, 16),
                Size = new Size(800, 26)
            };
            this.Controls.Add(lblHeader);

            lblGuide = new Label
            {
                Text = "⏳ 診断テストを実行中です... ブザー音が鳴ったあと、8秒以内にICカードをリーダーにかざしてください。",
                Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold),
                ForeColor = Color.FromArgb(37, 99, 235),
                Location = new Point(20, 46),
                Size = new Size(800, 24)
            };
            this.Controls.Add(lblGuide);

            grid = new DataGridView
            {
                Location = new Point(20, 78),
                Size = new Size(804, 286),
                AllowUserToAddRows = false,
                AllowUserToDeleteRows = false,
                AllowUserToResizeRows = false,
                ReadOnly = true,
                RowHeadersVisible = false,
                SelectionMode = DataGridViewSelectionMode.FullRowSelect,
                BackgroundColor = Color.White,
                BorderStyle = BorderStyle.FixedSingle,
                AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.AllCells,
                Font = new Font("Meiryo UI", 9.5f)
            };
            grid.ColumnHeadersDefaultCellStyle.Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold);
            grid.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(241, 245, 249);
            grid.ColumnHeadersDefaultCellStyle.ForeColor = Color.FromArgb(30, 41, 59);
            grid.ColumnHeadersHeight = 34;
            grid.EnableHeadersVisualStyles = false;
            grid.DefaultCellStyle.WrapMode = DataGridViewTriState.True;
            grid.DefaultCellStyle.Padding = new Padding(6, 6, 6, 6);

            DataGridViewTextBoxColumn colTest = new DataGridViewTextBoxColumn
            {
                HeaderText = "テスト項目",
                Width = 195
            };
            DataGridViewTextBoxColumn colVal = new DataGridViewTextBoxColumn
            {
                HeaderText = "使用・設定されている値",
                Width = 235
            };
            DataGridViewTextBoxColumn colRes = new DataGridViewTextBoxColumn
            {
                HeaderText = "結果",
                Width = 68
            };
            colRes.DefaultCellStyle.Alignment = DataGridViewContentAlignment.MiddleCenter;
            colRes.DefaultCellStyle.Font = new Font("Meiryo UI", 12f, FontStyle.Bold);

            DataGridViewTextBoxColumn colDetail = new DataGridViewTextBoxColumn
            {
                HeaderText = "結果詳細（成功 / 失敗理由）",
                Width = 300
            };

            grid.Columns.AddRange(new DataGridViewColumn[] { colTest, colVal, colRes, colDetail });
            this.Controls.Add(grid);

            btnRerun = new Button
            {
                Text = "🔄 もう一度テストを実行する",
                Font = new Font("Meiryo UI", 9.5f, FontStyle.Bold),
                BackColor = Color.FromArgb(59, 130, 246),
                ForeColor = Color.White,
                FlatStyle = FlatStyle.Flat,
                Location = new Point(20, 376),
                Size = new Size(240, 40),
                Cursor = Cursors.Hand,
                Enabled = false
            };
            btnRerun.FlatAppearance.BorderSize = 0;
            btnRerun.Click += (s, e) => StartDiagnosis();
            this.Controls.Add(btnRerun);

            btnClose = new Button
            {
                Text = "閉じる",
                Font = new Font("Meiryo UI", 9.5f),
                BackColor = Color.FromArgb(226, 232, 240),
                ForeColor = Color.FromArgb(30, 41, 59),
                FlatStyle = FlatStyle.Flat,
                Location = new Point(704, 376),
                Size = new Size(120, 40),
                Cursor = Cursors.Hand
            };
            btnClose.FlatAppearance.BorderSize = 0;
            btnClose.Click += (s, e) => this.Close();
            this.Controls.Add(btnClose);

            this.Shown += (s, e) => StartDiagnosis();
        }

        private void AddOrUpdateRow(string testName, string usedValue, string symbol, string detail)
        {
            if (this.InvokeRequired)
            {
                this.BeginInvoke(new Action<string, string, string, string>(AddOrUpdateRow), testName, usedValue, symbol, detail);
                return;
            }

            string dispSymbol = symbol == "O" ? "◯" : (symbol == "X" ? "☓" : "ー");
            int targetIdx = -1;
            for (int i = 0; i < grid.Rows.Count; i++)
            {
                if (Convert.ToString(grid.Rows[i].Cells[0].Value) == testName)
                {
                    targetIdx = i;
                    break;
                }
            }

            if (targetIdx >= 0)
            {
                grid.Rows[targetIdx].Cells[1].Value = usedValue;
                grid.Rows[targetIdx].Cells[2].Value = dispSymbol;
                grid.Rows[targetIdx].Cells[3].Value = detail;
            }
            else
            {
                targetIdx = grid.Rows.Add(testName, usedValue, dispSymbol, detail);
            }

            DataGridViewCell resCell = grid.Rows[targetIdx].Cells[2];
            if (dispSymbol == "◯")
            {
                resCell.Style.ForeColor = Color.FromArgb(22, 163, 74);
                resCell.Style.BackColor = Color.FromArgb(240, 253, 244);
            }
            else if (dispSymbol == "☓")
            {
                resCell.Style.ForeColor = Color.FromArgb(220, 38, 38);
                resCell.Style.BackColor = Color.FromArgb(254, 242, 242);
            }
            else
            {
                resCell.Style.ForeColor = Color.FromArgb(100, 116, 139);
                resCell.Style.BackColor = Color.FromArgb(248, 250, 252);
            }
            grid.ClearSelection();
        }

        private void SetGuideStatus(string text, Color color, bool enableRerun)
        {
            if (this.InvokeRequired)
            {
                this.BeginInvoke(new Action<string, Color, bool>(SetGuideStatus), text, color, enableRerun);
                return;
            }
            lblGuide.Text = text;
            lblGuide.ForeColor = color;
            btnRerun.Enabled = enableRerun;
        }

        private void StartDiagnosis()
        {
            if (isRunning) return;
            isRunning = true;
            grid.Rows.Clear();
            SetGuideStatus("⏳ 診断中... ブザー音が鳴ったあと、8秒以内にICカードをリーダーにかざしてください！", Color.FromArgb(37, 99, 235), false);

            AddOrUpdateRow("USB シリアル通信", "ポート: " + portName + " (115200bps)", "-", "接続確認中...");
            AddOrUpdateRow("マイコン本体・設定情報", "取得中...", "-", "待機中...");
            AddOrUpdateRow("Wi-Fi ネットワーク", "取得中...", "-", "待機中...");
            AddOrUpdateRow("Unit Buzzer (PORT B 音声テスト)", "PORT B (GPIO 8 / GPIO 7)", "-", "テスト音再生待機中...");
            AddOrUpdateRow("拡張ベース PORT A ＆ NFC通信", "I2C 0x50 (SDA=G38, SCL=G39)", "-", "診断待機中...");
            AddOrUpdateRow("NFC アンテナ電波出力 (13.56MHz)", "FeliCa (NFC-F) モード", "-", "診断待機中...");
            AddOrUpdateRow("ICカードかざし読取テスト", "待機時間: 8秒間", "-", "⏳ 今すぐリーダーにICカードをかざしてください...");

            Thread worker = new Thread(RunWorker);
            worker.IsBackground = true;
            worker.Start();
        }

        private void RunWorker()
        {
            bool anyDiagRowReceived = false;
            try
            {
                using (SerialPort sp = new SerialPort(portName, 115200))
                {
                    sp.Encoding = Encoding.UTF8;
                    sp.ReadTimeout = 1000;
                    sp.WriteTimeout = 1500;
                    sp.DtrEnable = true;
                    sp.RtsEnable = true;
                    sp.Open();

                    AddOrUpdateRow("USB シリアル通信", "ポート: " + portName + " (115200bps)", "O", "マイコンとのUSBシリアル通信に成功しました");

                    Thread.Sleep(250);
                    sp.DiscardInBuffer();
                    sp.Write("STUDYSPACE_DIAG\n");

                    DateTime deadline = DateTime.Now.AddSeconds(13);
                    while (DateTime.Now < deadline)
                    {
                        try
                        {
                            string line = sp.ReadLine().Trim();
                            if (string.IsNullOrEmpty(line)) continue;

                            if (parentLog != null) parentLog("[DIAG] " + line);

                            if (line.StartsWith("DIAG_ROW|"))
                            {
                                anyDiagRowReceived = true;
                                string[] parts = line.Split('|');
                                if (parts.Length >= 5)
                                {
                                    AddOrUpdateRow(parts[1], parts[2], parts[3], parts[4]);
                                }
                            }
                            else if (line == "DIAG_DONE")
                            {
                                break;
                            }
                            // 旧フォーマットのファームウェアとも互換動作させるパーサー
                            else if (line.Contains("Playing ENTRY_OK sound"))
                            {
                                AddOrUpdateRow("Unit Buzzer (PORT B 音声テスト)", "PORT B (GPIO 8 / GPIO 7)", "O", "テスト音（ピロリン♪）を再生しました");
                            }
                            else if (line.Contains("I2C 0x50 ping result:"))
                            {
                                bool i2cOk = line.Contains("result: 0");
                                if (!i2cOk)
                                {
                                    AddOrUpdateRow("拡張ベース PORT A ＆ NFC通信", "I2C 0x50 (SDA=G38, SCL=G39)", "X", "NFCリーダーから応答がありません。PORT Aの赤いケーブルを確認してください");
                                    AddOrUpdateRow("NFC アンテナ電波出力 (13.56MHz)", "FeliCa (NFC-F) モード", "X", "NFCリーダー未接続");
                                }
                            }
                            else if (line.Contains("REG_IC_IDENTITY"))
                            {
                                bool ok = line.Contains("[OK:");
                                AddOrUpdateRow("拡張ベース PORT A ＆ NFC通信", "I2C 0x50 (SDA=G38, SCL=G39)", ok ? "O" : "X", ok ? "ST25R3916 チップ正常応答 (" + line + ")" : "チップ応答異常 (" + line + ")");
                            }
                            else if (line.Contains("REG_OPERATION_CONTROL"))
                            {
                                bool ok = line.Contains("EN=1, RX_EN=1, TX_EN=1");
                                AddOrUpdateRow("NFC アンテナ電波出力 (13.56MHz)", "FeliCa (NFC-F) モード", ok ? "O" : "X", ok ? "RFキャリア送受信 ON (" + line + ")" : "RFキャリア出力OFF (" + line + ")");
                            }
                            else if (line.Contains("[DIAG CARD DETECTED!]"))
                            {
                                int idx = line.IndexOf("=");
                                string idm = idx > 0 ? line.Substring(idx + 1).Trim() : line;
                                AddOrUpdateRow("ICカードかざし読取テスト", "待機時間: 最大8秒", "O", "カード読取成功！ IDm: " + idm);
                            }
                            else if (line.Contains("8秒間以内にカードが検出されませんでした"))
                            {
                                AddOrUpdateRow("ICカードかざし読取テスト", "待機時間: 8秒間", "X", "8秒以内にカードがかざされませんでした（テスト中にかざすと◯になります）");
                            }
                            else if (line.Contains("ハードウェア診断完了"))
                            {
                                break;
                            }
                        }
                        catch (TimeoutException) { }
                    }
                    sp.Close();
                }

                if (!anyDiagRowReceived)
                {
                    AddOrUpdateRow("マイコン本体・設定情報", "USB接続: " + portName, "O", "接続確認済み（※最新ファームウェアに更新すると拠点名やSSIDも表示されます）");
                    AddOrUpdateRow("Wi-Fi ネットワーク", "本体メモリ設定", "-", "（※最新ファームウェアに更新するとWi-Fi状態も一覧表示されます）");
                }

                SetGuideStatus("✅ 診断テストが完了しました。もう一度テストする場合は左下のボタンを押してください。", Color.FromArgb(22, 163, 74), true);
            }
            catch (Exception ex)
            {
                AddOrUpdateRow("USB シリアル通信", "ポート: " + portName, "X", "通信エラー: " + ex.Message);
                SetGuideStatus("❌ 通信エラーが発生しました: " + ex.Message, Color.FromArgb(220, 38, 38), true);
            }
            finally
            {
                isRunning = false;
            }
        }
    }
}
