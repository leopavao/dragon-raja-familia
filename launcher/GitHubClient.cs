using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace DragonRaja.Family
{
    public sealed class ReleaseAsset
    {
        public long id { get; set; }
        public string name { get; set; }
        public long size { get; set; }
        public string digest { get; set; }
    }
    public sealed class FamilyRelease
    {
        public string tag_name { get; set; }
        public List<ReleaseAsset> assets { get; set; }
    }
    public sealed class GitHubClient : IDisposable
    {
        const string Api = "https://api.github.com/repos/leopavao/dragon-raja-familia/";
        readonly HttpClient client;
        readonly string token;
        public GitHubClient(string token, HttpMessageHandler transport = null)
        {
            this.token = token;
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            client = new HttpClient(transport ?? new HttpClientHandler { AllowAutoRedirect = false, UseCookies = false });
            client.Timeout = TimeSpan.FromMinutes(10);
        }
        static bool TrustedDownloadHost(Uri uri)
        {
            return uri.Scheme == "https" && (uri.Host == "api.github.com" || uri.Host == "github.com" ||
                uri.Host.EndsWith(".githubusercontent.com", StringComparison.OrdinalIgnoreCase));
        }
        async Task<HttpResponseMessage> GetAsync(string suffix, bool binary)
        {
            Uri uri = new Uri(Api + suffix);
            for (int redirect = 0; redirect < 6; ++redirect)
            {
                if (!TrustedDownloadHost(uri)) throw new InvalidDataException("Destino de download nao autorizado.");
                using (var request = new HttpRequestMessage(HttpMethod.Get, uri))
                {
                    request.Headers.UserAgent.ParseAdd("DragonRajaFamiliaLauncher/1.0");
                    request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(binary ? "application/octet-stream" : "application/vnd.github+json"));
                    // Never forward the GitHub token to the signed asset URL.
                    if (uri.Host == "api.github.com" && !String.IsNullOrWhiteSpace(token))
                        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
                    var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false);
                    if ((int)response.StatusCode >= 300 && (int)response.StatusCode < 400 && response.Headers.Location != null)
                    {
                        uri = new Uri(uri, response.Headers.Location);
                        response.Dispose();
                        continue;
                    }
                    if (!response.IsSuccessStatusCode)
                    {
                        int code = (int)response.StatusCode;
                        response.Dispose();
                        if (code == 401 || code == 403 || code == 404)
                            throw new InvalidOperationException("O GitHub nao liberou o acesso. Confira sua permissao no repo e o token de leitura.");
                        throw new IOException("O GitHub respondeu com HTTP " + code + ". Tente novamente mais tarde.");
                    }
                    return response;
                }
            }
            throw new IOException("O download excedeu o limite de redirecionamentos.");
        }
        public async Task<FamilyRelease> LatestAsync()
        {
            using (var response = await GetAsync("releases/latest", false).ConfigureAwait(false))
            {
                string json = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                if (json.Length > 2 * 1024 * 1024) throw new InvalidDataException("Resposta do GitHub acima do limite.");
                var release = new JavaScriptSerializer().Deserialize<FamilyRelease>(json);
                if (release == null || release.assets == null) throw new InvalidDataException("Release invalida.");
                return release;
            }
        }
        public async Task DownloadAsync(ReleaseAsset asset, string destination, Action<int> progress)
        {
            if (asset == null || asset.id <= 0 || asset.size <= 0 || asset.size > 2147483648L ||
                asset.digest == null || !asset.digest.StartsWith("sha256:", StringComparison.Ordinal))
                throw new InvalidDataException("O arquivo publicado nao informa tamanho e SHA-256 validos.");
            using (var response = await GetAsync("releases/assets/" + asset.id, true).ConfigureAwait(false))
            using (var input = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
            using (var output = File.Create(destination))
            {
                byte[] buffer = new byte[65536]; long received = 0; int count;
                while ((count = await input.ReadAsync(buffer,0,buffer.Length).ConfigureAwait(false)) > 0)
                {
                    received += count;
                    if (received > asset.size) throw new InvalidDataException("Download excedeu o tamanho publicado.");
                    await output.WriteAsync(buffer,0,count).ConfigureAwait(false);
                    progress((int)(received * 100 / asset.size));
                }
                if (received != asset.size) throw new InvalidDataException("Download incompleto.");
            }
            if (!UpdateEngine.HashFile(destination).Equals(asset.digest.Substring(7),StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("SHA-256 do download diverge do GitHub. Nada sera instalado.");
        }
        public void Dispose() { client.Dispose(); }
    }
}
