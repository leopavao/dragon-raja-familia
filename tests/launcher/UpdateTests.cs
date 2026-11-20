using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using DragonRaja.Family;

internal static class UpdateTests
{
    static int passed, failed;
    static string Hash(byte[] bytes) { using (var h = SHA256.Create()) return BitConverter.ToString(h.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant(); }
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static void Run(string name, Action test) {
        try { test(); ++passed; Console.WriteLine("PASS " + name); }
        catch (Exception e) { ++failed; Console.WriteLine("FAIL " + name + ": " + e.GetType().Name + " " + e.Message); }
    }
    static void Denies<T>(Action action) where T : Exception {
        try { action(); } catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name);
    }
    sealed class Fixture {
        public string root, zip;
        public UpdateManifest manifest;
        public Fixture() {
            string temp = Path.Combine(Path.GetTempPath(), "dragon-launcher-test-" + Guid.NewGuid().ToString("N"));
            root = Path.Combine(temp, "client"); zip = Path.Combine(temp, "update.zip");
            Directory.CreateDirectory(Path.Combine(root, "Data"));
            Directory.CreateDirectory(Path.Combine(root, "MyScreenshot"));
            File.WriteAllText(Path.Combine(root,"DragonRajaFamilia.exe"), "old-exe");
            File.WriteAllText(Path.Combine(root,"Data/abilitylimit.bin"), "old-limits");
            File.WriteAllText(Path.Combine(root,"dragon.ini"), "private-connection");
            File.WriteAllText(Path.Combine(root,"ddraw.ini"), "my-settings");
            File.WriteAllText(Path.Combine(root,"MyScreenshot/personal.jpg"), "personal-photo");
            manifest = new UpdateManifest { schema=1, version="1.0.1", package="client-update.zip", files=new List<ManagedFile>() };
        }
        public void Package(params string[] paths) {
            using (var stream = File.Create(zip)) using (var archive = new ZipArchive(stream,ZipArchiveMode.Create))
                foreach (string path in paths) {
                    byte[] bytes=Encoding.UTF8.GetBytes("new-"+path);
                    manifest.files.Add(new ManagedFile { path=path,size=bytes.Length,sha256=Hash(bytes) });
                    var entry=archive.CreateEntry(path);
                    using (var output=entry.Open()) output.Write(bytes,0,bytes.Length);
                }
        }
        public void Apply() { new UpdateEngine().Apply(root,zip,manifest,()=>false,s=>{}); }
        public string Read(string path) { return File.ReadAllText(Path.Combine(root,path)); }
        public void Originals() {
            Check(Read("DragonRajaFamilia.exe")=="old-exe","exe changed before validation or rollback failed");
            Check(Read("Data/abilitylimit.bin")=="old-limits","limits changed before validation or rollback failed");
            Personal();
        }
        public void Personal() {
            Check(Read("dragon.ini")=="private-connection","connection overwritten");
            Check(Read("ddraw.ini")=="my-settings","settings overwritten");
            Check(Read("MyScreenshot/personal.jpg")=="personal-photo","screenshot modified");
        }
    }
    static int Main() {
        Run("update and preserve personal files",()=>{
            var f=new Fixture(); f.Package("DragonRajaFamilia.exe","Data/abilitylimit.bin"); f.Apply();
            Check(f.Read("DragonRajaFamilia.exe")=="new-DragonRajaFamilia.exe","exe not installed"); f.Personal();
            Check(Directory.GetFiles(Path.Combine(f.root,".launcher","backups"),"DragonRajaFamilia.exe",SearchOption.AllDirectories).Length==1,"old exe backup missing");
        });
        Run("verify every digest before touching install",()=>{
            var f=new Fixture(); f.Package("DragonRajaFamilia.exe","Data/abilitylimit.bin"); f.manifest.files[1].sha256=new string('0',64);
            Denies<InvalidDataException>(()=>f.Apply()); f.Originals();
        });
        foreach (string bad in new[]{"../outside.exe","Data/../../outside.exe","C:/outside.exe","Data/file:stream","dragon.ini","MyScreenshot/personal.jpg","Character/profile.qm2","tools/install.ps1"}) {
            string path=bad;
            Run("reject unsafe or personal path "+path,()=>{ var f=new Fixture(); f.Package(path); Denies<InvalidDataException>(()=>f.Apply()); f.Originals(); });
        }
        Run("duplicate path case-insensitive",()=>{
            var f=new Fixture(); f.Package("DragonRajaFamilia.exe","dragonrajafamilia.exe"); Denies<InvalidDataException>(()=>f.Apply()); f.Originals();
        });
        Run("game open blocks writes",()=>{
            var f=new Fixture(); f.Package("DragonRajaFamilia.exe");
            Denies<InvalidOperationException>(()=>new UpdateEngine().Apply(f.root,f.zip,f.manifest,()=>true,s=>{})); f.Originals();
        });
        Run("rollback when second file is locked",()=>{
            var f=new Fixture(); f.Package("DragonRajaFamilia.exe","Data/abilitylimit.bin");
            using (var locked=new FileStream(Path.Combine(f.root,"Data/abilitylimit.bin"),FileMode.Open,FileAccess.Read,FileShare.Read)) {
                Denies<IOException>(()=>f.Apply()); f.Originals();
            }
        });
        Run("new patch accepted",()=>Check(UpdateEngine.ShouldUpdate("1.0.0","1.0.1"),"newer version ignored"));
        Run("local test build never downgraded",()=>Check(!UpdateEngine.ShouldUpdate("1.0.1","1.0.0"),"local candidate downgraded"));
        Run("same release not reapplied",()=>Check(!UpdateEngine.ShouldUpdate("1.0.1","1.0.1"),"same release reapplied"));
        Console.WriteLine(passed+" passed; "+failed+" failed"); return failed==0?0:1;
    }
}
