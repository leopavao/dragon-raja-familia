using System;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace DragonRaja.Family
{
    public static class CredentialStore
    {
        static readonly string FilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DragonRajaFamilia", "github-token.dat");
        static readonly byte[] Entropy = Encoding.UTF8.GetBytes("DragonRajaFamilia:GITHUB:read");
        public static void Save(string token)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath));
            File.WriteAllBytes(FilePath, ProtectedData.Protect(Encoding.UTF8.GetBytes(token), Entropy, DataProtectionScope.CurrentUser));
        }
        public static string Load()
        {
            if (File.Exists(FilePath))
                try { return Encoding.UTF8.GetString(ProtectedData.Unprotect(File.ReadAllBytes(FilePath), Entropy, DataProtectionScope.CurrentUser)); }
                catch (CryptographicException) { }
            // Reuse this Windows user's existing GitHub login; never ship a shared credential.
            try
            {
                var start = new ProcessStartInfo("git", "credential fill") {
                    UseShellExecute=false, CreateNoWindow=true, RedirectStandardInput=true,
                    RedirectStandardOutput=true, RedirectStandardError=true
                };
                start.EnvironmentVariables["GIT_TERMINAL_PROMPT"]="0";
                start.EnvironmentVariables["GCM_INTERACTIVE"]="never";
                using (var git = Process.Start(start))
                {
                    var output = git.StandardOutput.ReadToEndAsync();
                    var error = git.StandardError.ReadToEndAsync();
                    git.StandardInput.Write("protocol=https\nhost=github.com\n\n");
                    git.StandardInput.Close();
                    if (!git.WaitForExit(15000)) { git.Kill(); return null; }
                    string text = output.GetAwaiter().GetResult();
                    error.GetAwaiter().GetResult();
                    if (git.ExitCode != 0) return null;
                    foreach (string line in text.Split('\n'))
                        if (line.StartsWith("password=",StringComparison.Ordinal)) return line.Substring(9).TrimEnd('\r');
                }
            }
            catch (System.ComponentModel.Win32Exception) { }
            catch (InvalidOperationException) { }
            return null;
        }
    }
}
