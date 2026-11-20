using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace DragonRaja.Family
{
    public sealed class InstalledBuild
    {
        public string version { get; set; }
        public bool localBuild { get; set; }
    }
    public sealed class UpdateJob
    {
        public string root { get; set; }
        public string zip { get; set; }
        public string manifest { get; set; }
        public int parent { get; set; }
        public bool stayOpen { get; set; }
    }
    internal static class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            string root = AppDomain.CurrentDomain.BaseDirectory;
            if (args.Length == 2 && args[0] == "--check") return CheckAsync(args[1]).GetAwaiter().GetResult();
            UpdateJob job = null;
            if (args.Length == 2 && args[0] == "--apply-update")
            {
                try {
                    job = new JavaScriptSerializer().Deserialize<UpdateJob>(File.ReadAllText(args[1]));
                    root = Path.GetFullPath(job.root);
                    try { using(var parent=Process.GetProcessById(job.parent)) if(!parent.WaitForExit(30000)) return 1; }
                    catch(ArgumentException) { }
                } catch { MessageBox.Show("Nao foi possivel preparar a atualizacao. A instalacao foi mantida."); return 1; }
            }
            using (var mutex = new Mutex(false, "Local\\DragonRajaFamilyLauncher_" + RootId(root)))
            {
                if (!mutex.WaitOne(0)) { MessageBox.Show("O launcher ja esta aberto.","Dragon Raja Familia"); return 1; }
                try { Application.Run(new LauncherForm(root, job != null ? job.stayOpen : args.Contains("--stay-open"), job)); }
                finally { mutex.ReleaseMutex(); }
            }
            return 0;
        }
        static string RootId(string root)
        {
            using (var sha=System.Security.Cryptography.SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(Path.GetFullPath(root).ToUpperInvariant()))).Replace("-","").Substring(0,20);
        }
        static async Task<int> CheckAsync(string output)
        {
            try {
                using (var api=new GitHubClient(CredentialStore.Load())) {
                    var release=await api.LatestAsync();
                    File.WriteAllText(output,new JavaScriptSerializer().Serialize(new {
                        ok=true,release=release.tag_name,hasManifest=release.assets.Any(a=>a.name=="family-update.json")
                    }),Encoding.UTF8);
                }
                return 0;
            } catch {
                File.WriteAllText(output,"{\"ok\":false,\"error\":\"Falha ao consultar release. Nenhuma credencial foi registrada.\"}",Encoding.UTF8);
                return 1;
            }
        }
        public static bool GameRunning(string root)
        {
            string game=Path.GetFullPath(Path.Combine(root,"DragonRajaFamilia.exe"));
            foreach (var process in Process.GetProcessesByName("DragonRajaFamilia"))
                using (process) {
                    try { if (String.Equals(process.MainModule.FileName,game,StringComparison.OrdinalIgnoreCase)) return true; }
                    catch (System.ComponentModel.Win32Exception) { return true; }
                    catch (InvalidOperationException) { }
                }
            return false;
        }
    }
    internal sealed class AccessForm : Form
    {
        readonly TextBox token=new TextBox { UseSystemPasswordChar=true, Dock=DockStyle.Top };
        readonly CheckBox remember=new CheckBox { Text="Lembrar neste usuario do Windows",Checked=true,AutoSize=true };
        public string Token { get { return token.Text.Trim(); } }
        public AccessForm()
        {
            Text="Acesso ao repositorio privado"; ClientSize=new Size(490,225);
            FormBorderStyle=FormBorderStyle.FixedDialog; MaximizeBox=false; MinimizeBox=false;
            StartPosition=FormStartPosition.CenterParent;
            var info=new Label { Text="Use seu token pessoal do GitHub, com leitura de conteudo deste repositorio. Ele fica protegido neste Windows.",AutoSize=false,Bounds=new Rectangle(22,20,440,48) };
            token.SetBounds(22,80,440,27); token.Dock=DockStyle.None;
            remember.SetBounds(22,119,440,26);
            var save=new Button { Text="Conectar",DialogResult=DialogResult.OK,Bounds=new Rectangle(230,166,110,32) };
            var cancel=new Button { Text="Agora nao",DialogResult=DialogResult.Cancel,Bounds=new Rectangle(352,166,110,32) };
            save.Click+=(s,e)=> { if (Token.Length==0) { DialogResult=DialogResult.None; return; } if (remember.Checked) CredentialStore.Save(Token); };
            Controls.AddRange(new Control[]{info,token,remember,save,cancel}); AcceptButton=save; CancelButton=cancel;
        }
    }
    internal sealed class LauncherForm : Form
    {
        readonly string root;
        readonly bool stayOpen;
        readonly Label status=new Label();
        readonly Label detail=new Label();
        readonly ProgressBar bar=new ProgressBar();
        readonly Button play=new Button();
        readonly Button retry=new Button();
        readonly Button access=new Button();
        string token;
        bool working;
        public LauncherForm(string root,bool stayOpen,UpdateJob job=null)
        {
            this.root=root; this.stayOpen=stayOpen;
            Text="Dragon Raja Familia"; ClientSize=new Size(590,300);
            FormBorderStyle=FormBorderStyle.FixedSingle; MaximizeBox=false;
            StartPosition=FormStartPosition.CenterScreen; BackColor=Color.FromArgb(24,29,35);
            ForeColor=Color.FromArgb(235,238,240); Font=new Font("Segoe UI",10);
            var title=new Label { Text="DRAGON RAJA",Font=new Font("Segoe UI",25,FontStyle.Bold),Bounds=new Rectangle(26,20,540,46) };
            var subtitle=new Label { Text="FAMILIA  /  ATUALIZACOES",ForeColor=Color.FromArgb(213,177,107),Bounds=new Rectangle(30,70,520,25) };
            status.SetBounds(30,115,530,28); status.Text="Procurando atualizacoes...";
            detail.SetBounds(30,148,530,56); detail.Text="Suas configuracoes e screenshots ficam preservados.";
            detail.ForeColor=Color.FromArgb(177,187,197);
            bar.SetBounds(30,209,530,8); bar.Style=ProgressBarStyle.Marquee;
            play.Text="Jogar"; play.SetBounds(430,240,130,36); play.Enabled=false;
            retry.Text="Verificar novamente"; retry.SetBounds(30,240,180,36);
            access.Text="Acesso ao GitHub"; access.SetBounds(224,240,185,36);
            foreach(var button in new[]{play,retry,access}) { button.FlatStyle=FlatStyle.Flat; button.BackColor=Color.FromArgb(43,51,61); button.ForeColor=ForeColor; }
            play.BackColor=Color.FromArgb(108,77,34);
            Controls.AddRange(new Control[]{title,subtitle,status,detail,bar,play,retry,access});
            play.Click+=(s,e)=>LaunchGame();
            retry.Click+=async(s,e)=>await UpdateAsync();
            access.Click+=async(s,e)=> { using(var dialog=new AccessForm()) if(dialog.ShowDialog(this)==DialogResult.OK) { token=dialog.Token; await UpdateAsync(); } };
            Shown+=async(s,e)=> {
                token=await Task.Run(()=>CredentialStore.Load());
                if(job!=null) await ApplyJobAsync(job);
                else await UpdateAsync();
            };
            FormClosing+=(s,e)=> { if(working) { e.Cancel=true; status.Text="Concluindo a verificacao com seguranca..."; } };
        }
        void Report(string text)
        {
            if(IsDisposed) return;
            if(InvokeRequired) { BeginInvoke(new Action<string>(Report),text); return; }
            detail.Text=text;
        }
        string Receipt { get { return Path.Combine(root,".launcher","installed.json"); } }
        InstalledBuild ReadInstalled()
        {
            try { if(File.Exists(Receipt)) return new JavaScriptSerializer().Deserialize<InstalledBuild>(File.ReadAllText(Receipt)); }
            catch { }
            return new InstalledBuild { version="0.0.0" };
        }
        void WriteInstalled(string version)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Receipt));
            string temp=Receipt+".new";
            File.WriteAllText(temp,new JavaScriptSerializer().Serialize(new InstalledBuild { version=version,localBuild=false }),Encoding.UTF8);
            if(File.Exists(Receipt)) File.Replace(temp,Receipt,null); else File.Move(temp,Receipt);
        }
        void StartUpdateHelper(string zip,string manifest,string directory)
        {
            string helper=Path.Combine(directory,"DragonRajaUpdater.exe");
            string self=Assembly.GetExecutingAssembly().Location;
            File.Copy(self,helper,false);
            if(UpdateEngine.HashFile(self)!=UpdateEngine.HashFile(helper)) throw new IOException("Falha ao preparar o atualizador.");
            var job=new UpdateJob { root=root,zip=zip,manifest=manifest,parent=Process.GetCurrentProcess().Id,stayOpen=stayOpen };
            string jobPath=Path.Combine(directory,"apply-job.json");
            File.WriteAllText(jobPath,new JavaScriptSerializer().Serialize(job),Encoding.UTF8);
            Process.Start(new ProcessStartInfo(helper,"--apply-update \""+jobPath+"\"") { WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true });
            working=false;
            Close();
        }
        async Task ApplyJobAsync(UpdateJob job)
        {
            working=true; play.Enabled=retry.Enabled=access.Enabled=false;
            status.Text="Verificando e instalando...";
            bool ready=false;
            try {
                var manifest=new JavaScriptSerializer().Deserialize<UpdateManifest>(File.ReadAllText(job.manifest));
                await Task.Run(()=>new UpdateEngine().Apply(root,job.zip,manifest,()=>Program.GameRunning(root),Report));
                WriteInstalled(manifest.version);
                status.Text="Atualizado para "+manifest.version; ready=true;
            } catch(Exception e) {
                status.Text="Atualizacao nao aplicada";
                Report(e is IOException || e is InvalidDataException || e is InvalidOperationException ? e.Message : "Falha ao instalar. Confira o backup em .launcher/backups antes de tentar novamente.");
            } finally {
                working=false; bar.Style=ProgressBarStyle.Continuous; bar.Value=ready?100:0;
                play.Enabled=File.Exists(Path.Combine(root,"DragonRajaFamilia.exe"))&&!Program.GameRunning(root);
                retry.Enabled=access.Enabled=true;
            }
            if(ready&&!stayOpen) { await Task.Delay(700); if(!IsDisposed) LaunchGame(); }
        }
        async Task UpdateAsync()
        {
            if(working) return;
            working=true; play.Enabled=retry.Enabled=access.Enabled=false;
            bar.Style=ProgressBarStyle.Marquee; status.Text="Conferindo a versao publicada...";
            bool ready=false;
            try
            {
                if(Program.GameRunning(root)) throw new InvalidOperationException("O jogo ja esta aberto. Feche-o para verificar e instalar atualizacoes.");
                using(var api=new GitHubClient(token))
                {
                    var release=await api.LatestAsync();
                    string version=(release.tag_name??"").TrimStart('v');
                    var installed=ReadInstalled();
                    if(!UpdateEngine.ShouldUpdate(installed.version,version))
                    {
                        status.Text="Pronto para jogar";
                        Report(installed.localBuild ? "Build local de teste preservado. Publicacao pendente da confirmacao em jogo." : "Seu cliente ja esta atualizado: "+installed.version);
                        ready=true;
                    }
                    else
                    {
                        var manifestAsset=release.assets.FirstOrDefault(a=>a.name=="family-update.json");
                        if(manifestAsset==null) throw new InvalidOperationException("A release "+release.tag_name+" ainda usa o pacote antigo. Esta instalacao foi mantida; atualizacoes automaticas passam a valer nas proximas releases.");
                        if(manifestAsset.size>2*1024*1024) throw new InvalidDataException("Manifesto acima do limite permitido.");
                        string download=Path.Combine(root,".launcher","downloads",Guid.NewGuid().ToString("N"));
                        Directory.CreateDirectory(download);
                        string manifestPath=Path.Combine(download,"family-update.json");
                        await api.DownloadAsync(manifestAsset,manifestPath,p=>{});
                        var manifest=new JavaScriptSerializer().Deserialize<UpdateManifest>(File.ReadAllText(manifestPath));
                        if(manifest==null || manifest.version!=version || manifest.package==null ||
                           !System.Text.RegularExpressions.Regex.IsMatch(manifest.package,@"^[A-Za-z0-9._-]+\.zip$"))
                            throw new InvalidDataException("Versao ou pacote do manifesto invalido.");
                        var package=release.assets.FirstOrDefault(a=>a.name==manifest.package);
                        if(package==null) throw new InvalidDataException("Pacote desta versao nao encontrado.");
                        status.Text="Baixando versao "+version; bar.Style=ProgressBarStyle.Continuous;
                        string zip=Path.Combine(download,manifest.package);
                        var progress=new Progress<int>(p=> { if(!IsDisposed) bar.Value=Math.Max(0,Math.Min(100,p)); });
                        await api.DownloadAsync(package,zip,p=>((IProgress<int>)progress).Report(p));
                        status.Text="Preparando instalacao...";
                        StartUpdateHelper(zip,manifestPath,download);
                        return;
                    }
                }
            }
            catch(InvalidDataException e) { status.Text="Atualizacao nao aplicada"; Report(e.Message); }
            catch(InvalidOperationException e) { status.Text="Verificacao nao concluida"; Report(e.Message); }
            catch(IOException e) { status.Text="Nao foi possivel atualizar"; Report(e.Message); }
            catch { status.Text="Nao foi possivel consultar o GitHub"; Report("Confira sua conexao e o acesso ao repo. Voce pode jogar com a instalacao atual."); }
            finally
            {
                working=false;
                if(!IsDisposed) {
                    bar.Style=ProgressBarStyle.Continuous; bar.Value=ready?100:0;
                    retry.Enabled=access.Enabled=true;
                    play.Enabled=File.Exists(Path.Combine(root,"DragonRajaFamilia.exe"))&&!Program.GameRunning(root);
                }
            }
            if(ready && !stayOpen) { await Task.Delay(700); if(!working && !IsDisposed) LaunchGame(); }
        }
        void LaunchGame()
        {
            if(working || Program.GameRunning(root)) return;
            try {
                Process.Start(new ProcessStartInfo(Path.Combine(root,"DragonRajaFamilia.exe")) { WorkingDirectory=root,UseShellExecute=true });
                Close();
            } catch { status.Text="Nao foi possivel abrir o jogo"; Report("Confira se DragonRajaFamilia.exe esta nesta pasta e tente novamente."); }
        }
    }
}
