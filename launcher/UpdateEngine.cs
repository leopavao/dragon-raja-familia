using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace DragonRaja.Family
{
    public sealed class ManagedFile
    {
        public string path { get; set; }
        public long size { get; set; }
        public string sha256 { get; set; }
    }
    public sealed class UpdateManifest
    {
        public int schema { get; set; }
        public string version { get; set; }
        public string package { get; set; }
        public List<ManagedFile> files { get; set; }
    }
    public sealed class UpdateEngine
    {
        public static string HashFile(string path)
        {
            using (var file = File.OpenRead(path))
            using (var hash = SHA256.Create())
                return BitConverter.ToString(hash.ComputeHash(file)).Replace("-", "").ToLowerInvariant();
        }

        public static bool ShouldUpdate(string installed, string available)
        {
            Version remote, local;
            if (!Version.TryParse(available, out remote)) throw new InvalidDataException("Versao publicada invalida.");
            return !Version.TryParse(installed, out local) || remote > local;
        }

        static readonly HashSet<string> AssetFolders = new HashSet<string>(new[] {
            "Achievements", "Char", "Data", "Effect", "Event", "Item", "Itemdata",
            "Itemtable", "Magic", "Map", "Object", "Skill", "Sound"
        }, StringComparer.OrdinalIgnoreCase);

        public static string ValidatePath(string relative)
        {
            if (String.IsNullOrWhiteSpace(relative) || relative.Length > 220 || relative.IndexOf('\\') >= 0)
                throw new InvalidDataException("Caminho de atualizacao invalido.");
            var parts = relative.Split('/');
            foreach (string part in parts)
            {
                if (part.Length == 0 || part == "." || part == ".." || part.EndsWith(".") || part.EndsWith(" ") ||
                    part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                    Regex.IsMatch(part, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)", RegexOptions.IgnoreCase))
                    throw new InvalidDataException("Caminho de atualizacao inseguro.");
            }
            string extension = Path.GetExtension(relative);
            if (new[] { ".ini", ".cfg", ".json", ".log", ".qm2", ".ps1", ".cmd", ".bat" }.Contains(extension.ToLowerInvariant()))
                throw new InvalidDataException("O pacote tenta alterar configuracoes ou dados locais.");
            bool allowed = parts.Length == 1
                ? relative.Equals("DragonRajaFamilia.exe", StringComparison.OrdinalIgnoreCase) ||
                  relative.Equals("DragonRajaLauncher.exe", StringComparison.OrdinalIgnoreCase) ||
                  relative.Equals("CrashSender1300.exe", StringComparison.OrdinalIgnoreCase) ||
                  extension.Equals(".dll", StringComparison.OrdinalIgnoreCase)
                : AssetFolders.Contains(parts[0]);
            if (!allowed) throw new InvalidDataException("Arquivo fora das pastas gerenciadas do jogo.");
            return relative;
        }

        static void RejectLinks(string path)
        {
            for (string current = Path.GetFullPath(path); !String.IsNullOrEmpty(current); current = Path.GetDirectoryName(current))
                if ((File.Exists(current) || Directory.Exists(current)) &&
                    (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Atualizacao recusada: pasta ou arquivo redirecionado.");
        }

        static string Destination(string root, string relative)
        {
            string full = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
            string prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            if (!full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Arquivo fora da instalacao.");
            RejectLinks(full);
            return full;
        }

        public void Apply(string root, string zip, UpdateManifest manifest, Func<bool> gameRunning, Action<string> progress)
        {
            if (gameRunning()) throw new InvalidOperationException("Feche o jogo antes de atualizar.");
            RejectLinks(root);
            Version version;
            if (manifest == null || manifest.schema != 1 || !Version.TryParse(manifest.version, out version) ||
                manifest.files == null || manifest.files.Count < 1 || manifest.files.Count > 20000)
                throw new InvalidDataException("Manifesto de atualizacao invalido.");
            var files = new Dictionary<string, ManagedFile>(StringComparer.OrdinalIgnoreCase);
            long total = 0;
            foreach (var file in manifest.files)
            {
                if (file == null) throw new InvalidDataException("Entrada de arquivo ausente.");
                ValidatePath(file.path);
                Destination(root, file.path);
                if (files.ContainsKey(file.path) || file.size < 0 || file.size > 2147483648L ||
                    file.sha256 == null || !Regex.IsMatch(file.sha256, "^[a-fA-F0-9]{64}$"))
                    throw new InvalidDataException("Lista de arquivos invalida ou duplicada.");
                total += file.size;
                if (total > 4294967296L) throw new InvalidDataException("Pacote excede o limite de tamanho.");
                files.Add(file.path, file);
            }
            string id = DateTime.Now.ToString("yyyy-MM-dd_HHmmss") + "_" + Guid.NewGuid().ToString("N").Substring(0,8);
            string stage = Destination(root, ".launcher/staging/" + id);
            string backup = Destination(root, ".launcher/backups/" + id);
            Directory.CreateDirectory(stage);
            var found = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            // Finish verification of the ENTIRE package before touching the installation.
            using (var archive = ZipFile.OpenRead(zip))
            {
                foreach (var entry in archive.Entries)
                {
                    if (entry.FullName.EndsWith("/")) continue;
                    ManagedFile file;
                    if (!files.TryGetValue(entry.FullName, out file) || !found.Add(entry.FullName) ||
                        entry.Length != file.size || ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000)
                        throw new InvalidDataException("Conteudo do pacote diverge do manifesto.");
                    string staged = Destination(stage, file.path);
                    Directory.CreateDirectory(Path.GetDirectoryName(staged));
                    using (var input = entry.Open()) using (var output = File.Create(staged)) input.CopyTo(output);
                    if (new FileInfo(staged).Length != file.size || !HashFile(staged).Equals(file.sha256, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Verificacao SHA-256 falhou. Nada foi instalado.");
                }
            }
            if (found.Count != files.Count) throw new InvalidDataException("O pacote esta incompleto.");
            if (gameRunning()) throw new InvalidOperationException("O jogo foi aberto durante a verificacao. Feche-o antes de atualizar.");
            var changed = manifest.files.Where(f => !File.Exists(Destination(root,f.path)) ||
                !HashFile(Destination(root,f.path)).Equals(f.sha256,StringComparison.OrdinalIgnoreCase)).ToList();
            Directory.CreateDirectory(backup);
            foreach (var file in changed)
            {
                string target = Destination(root,file.path);
                if (!File.Exists(target)) continue;
                string old = Destination(backup,file.path);
                Directory.CreateDirectory(Path.GetDirectoryName(old));
                File.Copy(target,old,false);
            }
            var applied = new List<ManagedFile>();
            try
            {
                foreach (var file in changed)
                {
                    if (gameRunning()) throw new IOException("O jogo foi aberto durante a atualizacao.");
                    progress("Instalando " + file.path);
                    string target = Destination(root,file.path), staged = Destination(stage,file.path);
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    if (File.Exists(target)) File.Replace(staged,target,null);
                    else File.Move(staged,target);
                    applied.Add(file);
                    if (!HashFile(target).Equals(file.sha256,StringComparison.OrdinalIgnoreCase))
                        throw new IOException("Verificacao apos instalacao falhou.");
                }
            }
            catch (Exception failure)
            {
                bool rollbackFailed = false;
                applied.Reverse();
                foreach (var file in applied)
                {
                    try
                    {
                        string target = Destination(root,file.path), old = Destination(backup,file.path);
                        if (File.Exists(old))
                        {
                            string restore = Destination(stage,file.path + ".restore");
                            Directory.CreateDirectory(Path.GetDirectoryName(restore));
                            File.Copy(old,restore,true);
                            File.Replace(restore,target,null);
                        }
                        else File.Delete(target); // Only a file this transaction just created.
                    }
                    catch { rollbackFailed = true; }
                }
                throw new IOException((rollbackFailed ? "Falha ao restaurar automaticamente. Backup em: " :
                    "Atualizacao cancelada; arquivos anteriores restaurados. Backup em: ") + backup, failure);
            }
            progress("Atualizacao verificada. Backup em: " + backup);
        }
    }
}
