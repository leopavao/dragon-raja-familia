using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using DragonRaja.Family;

internal static class GitHubTests
{
    sealed class Transport : HttpMessageHandler {
        public Func<HttpRequestMessage,int,HttpResponseMessage> respond;
        public int calls;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken cancellation) {
            return Task.FromResult(respond(request,calls++));
        }
    }
    static string Hash(byte[] bytes) { using(var h=SHA256.Create()) return BitConverter.ToString(h.ComputeHash(bytes)).Replace("-","").ToLowerInvariant(); }
    static void Check(bool pass,string message) { if(!pass) throw new Exception(message); }
    static ReleaseAsset Asset(byte[] bytes) { return new ReleaseAsset { id=1,name="test.zip",size=bytes.Length,digest="sha256:"+Hash(bytes) }; }
    static string Output() { return Path.Combine(Path.GetTempPath(),"dragon-http-test-"+Guid.NewGuid().ToString("N")+".zip"); }
    static void Reject<T>(Action action) where T:Exception { try { action(); } catch(T) { return; } throw new Exception("Expected "+typeof(T).Name); }
    static int Main() {
      try {
        byte[] bytes=Encoding.UTF8.GetBytes("verified-package");
        var transport=new Transport { respond=(request,call)=> {
            if(call==0) {
                Check(request.Headers.Authorization!=null,"API request needs auth");
                var redirect=new HttpResponseMessage(HttpStatusCode.Found);
                redirect.Headers.Location=new Uri("https://release-assets.githubusercontent.com/example"); return redirect;
            }
            Check(request.Headers.Authorization==null,"credential forwarded to asset host");
            return new HttpResponseMessage(HttpStatusCode.OK) { Content=new ByteArrayContent(bytes) };
        }};
        string file=Output();
        using(var api=new GitHubClient("test-only-sentinel",transport)) api.DownloadAsync(Asset(bytes),file,p=>{}).GetAwaiter().GetResult();
        Check(UpdateEngine.HashFile(file)==Hash(bytes),"download changed");
        Console.WriteLine("PASS authenticated download strips credentials on redirect");
        transport=new Transport { respond=(request,call)=> {
            var redirect=new HttpResponseMessage(HttpStatusCode.Found);
            redirect.Headers.Location=new Uri("https://example.invalid/untrusted"); return redirect;
        }};
        using(var api=new GitHubClient("test-only-sentinel",transport))
            Reject<InvalidDataException>(()=>api.DownloadAsync(Asset(bytes),Output(),p=>{}).GetAwaiter().GetResult());
        Check(transport.calls==1,"untrusted host was contacted"); Console.WriteLine("PASS untrusted redirect rejected");
        transport=new Transport { respond=(r,c)=>new HttpResponseMessage(HttpStatusCode.OK) { Content=new ByteArrayContent(bytes) } };
        var bad=Asset(bytes); bad.digest="sha256:"+new string('0',64);
        using(var api=new GitHubClient("test-only-sentinel",transport))
            Reject<InvalidDataException>(()=>api.DownloadAsync(bad,Output(),p=>{}).GetAwaiter().GetResult());
        Console.WriteLine("PASS corrupted download rejected");
        transport=new Transport { respond=(r,c)=>new HttpResponseMessage(HttpStatusCode.OK) { Content=new ByteArrayContent(new byte[]{1}) } };
        using(var api=new GitHubClient("test-only-sentinel",transport))
            Reject<InvalidDataException>(()=>api.DownloadAsync(Asset(bytes),Output(),p=>{}).GetAwaiter().GetResult());
        Console.WriteLine("PASS incomplete download rejected");
        return 0;
      } catch(Exception e) { Console.WriteLine("FAIL GitHub boundary: "+e.Message); return 1; }
    }
}
